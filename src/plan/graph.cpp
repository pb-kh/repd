#include "plan/graph.hpp"

#include <algorithm>
#include <deque>
#include <format>
#include <queue>
#include <set>

#include "plan/scope.hpp"
#include "util/strings.hpp"

namespace repd {

namespace {

// Returns the chain "a → b → … → a" for the first cycle found, or "".
std::string find_cycle(const std::map<std::string, std::vector<std::string>>& deps) {
  enum Color { White, Grey, Black };
  std::map<std::string, Color> color;
  std::vector<std::string> stack;
  std::string chain;
  auto visit = [&](auto&& self, const std::string& n) -> bool {
    color[n] = Grey;
    stack.push_back(n);
    auto it = deps.find(n);
    if (it != deps.end()) {
      std::vector<std::string> next = it->second;
      std::ranges::sort(next);
      for (const auto& d : next) {
        if (color[d] == Grey) {
          auto start = std::ranges::find(stack, d);
          std::vector<std::string> cyc(start, stack.end());
          cyc.push_back(d);
          chain = join(cyc, " → ");
          return true;
        }
        if (color[d] == White && self(self, d)) return true;
      }
    }
    stack.pop_back();
    color[n] = Black;
    return false;
  };
  for (const auto& [n, _] : deps) {
    if (color[n] == White && visit(visit, n)) return chain;
  }
  return {};
}

}  // namespace

Result<std::vector<std::string>> topo_order(
    const std::map<std::string, std::vector<std::string>>& deps) {
  if (const std::string chain = find_cycle(deps); !chain.empty()) {
    return fail(ec::kCycle, std::format("modules depend on each other in a cycle: {}", chain),
                "remove one of these requires so the dependencies form a tree");
  }
  std::map<std::string, int> pending;
  std::map<std::string, std::vector<std::string>> dependents;
  for (const auto& [n, ds] : deps) {
    pending.try_emplace(n, 0);
    for (const auto& d : std::set<std::string>(ds.begin(), ds.end())) {
      pending.try_emplace(d, 0);
      ++pending[n];
      dependents[d].push_back(n);
    }
  }
  std::priority_queue<std::string, std::vector<std::string>, std::greater<>> ready;
  for (const auto& [n, c] : pending) {
    if (c == 0) ready.push(n);
  }
  std::vector<std::string> order;
  while (!ready.empty()) {
    std::string n = ready.top();
    ready.pop();
    order.push_back(n);
    for (const auto& m : dependents[n]) {
      if (--pending[m] == 0) ready.push(m);
    }
  }
  return order;
}

std::pair<std::string, bool> split_extras(const std::string& name,
                                          const std::map<std::string, ModuleMeta>& modules) {
  if (modules.contains(name) || !name.ends_with('+')) return {name, false};
  return {name.substr(0, name.size() - 1), true};
}

Result<Closure> select_scope(const std::map<std::string, ModuleMeta>& modules,
                             const std::vector<std::string>& roots, Resolver& resolver,
                             const std::string& root_source, const std::map<std::string, bool>& auto_sync) {
  std::vector<std::string> known;
  for (const auto& [n, _] : modules) known.push_back(n);
  std::map<std::string, bool> wanted;  // root -> with its optional requires
  for (const auto& r : roots) {
    auto [name, extras] = split_extras(r, modules);
    if (!modules.contains(name)) {
      std::string hint = suggest_module(name, known);
      return fail(ec::kUnknown, std::format("unknown module '{}'", r),
                  hint.empty() ? "list available modules with `repd list modules`" : hint);
    }
    wanted[name] = wanted[name] || extras;
  }
  // A provider selected directly satisfies its interface (FR-I-5).
  for (const auto& [r, _] : wanted) {
    const ModuleMeta& m = modules.at(r);
    if (m.is_interface() || !m.provides || resolver.has_cli_override(*m.provides)) continue;
    if (!resolver.resolved().contains(*m.provides)) resolver.choose(*m.provides, r, root_source);
  }

  Closure c;
  std::set<std::string> seen;
  std::deque<std::string> queue;
  // Map a requested name (module or interface) to the module that fulfils it.
  auto target = [&](const std::string& name,
                    const std::vector<std::string>& requirers) -> Result<std::string> {
    const ModuleMeta& m = modules.at(name);
    if (!m.is_interface()) return name;
    auto p = resolver.resolve(name, requirers);
    if (!p) return std::unexpected(p.error());
    c.providers[name] = *p;
    return p->provider;
  };
  auto add = [&](const std::string& n) {
    if (seen.insert(n).second) queue.push_back(n);
  };
  auto unknown_requirement = [&](const std::string& n, const std::string& req) {
    std::string hint = suggest_module(req, known);
    return fail(ec::kUnknown, std::format("module {} requires '{}', which does not exist", n, req),
                hint.empty() ? std::format("create modules/{}.lua or fix the name", req) : hint);
  };
  auto named = [&](const std::string& t) {
    if (std::ranges::find(c.named, t) == c.named.end()) c.named.push_back(t);
    add(t);
  };
  for (const auto& [r, extras] : wanted) {
    auto t = target(r, {});
    if (!t) return std::unexpected(t.error());
    named(*t);
    if (!extras) continue;
    for (const auto& opt : modules.at(*t).optional_) {
      if (!modules.contains(opt)) return unknown_requirement(*t, opt);
      auto o = target(opt, {*t});
      if (!o) return std::unexpected(o.error());
      named(*o);
    }
  }
  // Requires and integrated modules are synced along; optional requires are not.
  auto drain = [&]() -> Status {
    while (!queue.empty()) {
      const std::string n = queue.front();
      queue.pop_front();
      auto& deps = c.deps[n];
      const ModuleMeta& m = modules.at(n);
      for (const auto* list : {&m.requires_, &m.integrates}) {
        for (const auto& req : *list) {
          if (!modules.contains(req)) return unknown_requirement(n, req);
          auto t = target(req, {n});
          if (!t) return std::unexpected(t.error());
          deps.push_back(*t);
          add(*t);
        }
      }
    }
    return {};
  };
  // Synced already: a module, or an interface whose chosen provider is (never prompts).
  auto in_scope = [&](const std::string& name) {
    if (!modules.at(name).is_interface()) return seen.contains(name);
    auto it = resolver.resolved().find(name);
    return it != resolver.resolved().end() && seen.contains(it->second.provider);
  };
  auto syncs_itself = [&](const std::string& name) {
    auto it = auto_sync.find(name);
    return it != auto_sync.end() ? it->second : modules.at(name).auto_sync;
  };
  // Integrations join once everything they integrate is synced, which can
  // bring in more modules and so complete further integrations.
  for (bool grew = true; grew;) {
    if (auto d = drain(); !d) return std::unexpected(d.error());
    grew = false;
    for (const auto& [name, m] : modules) {
      if (m.integrates.empty() || seen.contains(name) || !syncs_itself(name)) continue;
      if (std::ranges::all_of(m.integrates, in_scope)) {
        add(name);
        grew = true;
      }
    }
  }
  for (const auto& [name, m] : modules) {
    if (m.integrates.empty()) continue;
    IntegrationStatus st{name, seen.contains(name), false, {}};
    for (const auto& i : m.integrates) {
      if (!in_scope(i)) st.missing.push_back(i);
    }
    // Only those touching this selection: some of their modules are synced.
    if (!st.active && st.missing.size() == m.integrates.size()) continue;
    st.held = !st.active && st.missing.empty();
    c.integrations.push_back(std::move(st));
  }
  for (const auto& n : seen) {
    for (const auto& opt : modules.at(n).optional_) {
      if (!in_scope(opt)) c.optional[n].push_back(opt);
    }
  }
  // Providers chosen directly as roots are recorded even if nothing required the interface.
  for (const auto& [iface, p] : resolver.resolved()) {
    if (seen.contains(p.provider)) c.providers.try_emplace(iface, p);
  }
  auto order = topo_order(c.deps);
  if (!order) return std::unexpected(order.error());
  c.order = std::move(*order);
  return c;
}

}  // namespace repd
