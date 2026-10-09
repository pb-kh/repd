#include "eval/evaluate.hpp"

#include <format>

#include "plan/graph.hpp"
#include "plan/scope.hpp"

namespace repro {

Evaluator::Evaluator(LuaHost& host, const Fs& fs, ConfigRepository repo)
    : host_(host), fs_(fs), repo_(std::move(repo)) {}

Status Evaluator::configure() {
  if (configured_) return {};
  auto r = host_.call("configure", {Map{{"config_root", repo_.root}}});
  if (!r) return std::unexpected(r.error());
  configured_ = true;
  return {};
}

Result<RootConfig> Evaluator::load_root() {
  if (auto r = check_repository(fs_, repo_); !r) return std::unexpected(r.error());
  if (auto r = configure(); !r) return std::unexpected(r.error());
  PhaseScope eval(host_, Phase::Eval);
  auto v = host_.call("load_root", {repo_.entry, "repro.lua"});
  if (!v) return std::unexpected(v.error());
  return RootConfig::from_value(*v);
}

Result<std::vector<LuaBackendSpec>> Evaluator::load_lua_backends() {
  if (auto r = configure(); !r) return std::unexpected(r.error());
  PhaseScope eval(host_, Phase::Eval);
  std::vector<LuaBackendSpec> out;
  for (const auto& p : discover_lua_plugins(fs_, repo_)) {
    auto v = host_.call("load_backend", {p.path, p.rel});
    if (!v) return std::unexpected(v.error());
    out.push_back(LuaBackendSpec{v->string_or("namespace", ""), v->string_or("name", ""),
                                 v->string_or("privilege", "system"), v->string_or("version", "lua"),
                                 p.rel, (*v)["upgrade"].as_bool(), (*v)["upgrade_packages"].as_bool()});
  }
  return out;
}

Result<std::map<std::string, ModuleMeta>> Evaluator::load_modules() {
  if (auto r = configure(); !r) return std::unexpected(r.error());
  auto found = discover_modules(fs_, repo_);
  if (!found) return std::unexpected(found.error());
  PhaseScope eval(host_, Phase::Eval);
  std::map<std::string, ModuleMeta> out;
  for (const auto& [name, d] : *found) {
    auto v = host_.call("load_module", {name, d.path, d.rel});
    if (!v) return std::unexpected(v.error());
    out.emplace(name, ModuleMeta::from_value(*v));
  }
  return out;
}

Status Evaluator::validate(RootConfig& root,
                           const std::map<std::string, ModuleMeta>& modules) {
  std::vector<std::string> names;
  for (const auto& [n, m] : modules) names.push_back(n);
  auto unknown = [&](const std::string& what, const std::string& name) {
    std::string hint = suggest_module(name, names);
    if (hint.empty()) hint = std::format("create modules/{}.lua or fix the name", name);
    return fail(ec::kUnknown, std::format("{} '{}', which does not exist", what, name), hint);
  };
  for (const auto& [n, m] : modules) {
    if (m.provides) {
      auto it = modules.find(*m.provides);
      if (it == modules.end()) {
        return unknown(std::format("module {} provides interface", n), *m.provides);
      }
      if (!it->second.is_interface()) {
        return fail(ec::kConfig,
                    std::format("module {} provides {}, which is a module, not an interface", n,
                                *m.provides),
                    std::format("declare modules/{}.lua with rp.interface {{ ... }}", *m.provides));
      }
    }
    for (const auto* list : {&m.requires_, &m.optional_}) {
      for (const auto& r : *list) {
        if (!modules.contains(r)) return unknown(std::format("module {} requires", n), r);
        if (r == n) {
          return fail(ec::kCycle, std::format("module {} requires itself: {} → {}", n, n, n),
                      "remove the self-reference from requires");
        }
      }
    }
    for (const auto& r : m.integrates) {
      if (!modules.contains(r)) return unknown(std::format("module {} integrates", n), r);
      if (r == n) {
        return fail(ec::kConfig, std::format("module {} integrates itself", n),
                    "list the modules it connects, e.g. integrates = { \"shell/fish\", \"dev/rust\" }");
      }
    }
  }
  for (auto& p : root.profiles) {
    for (auto& u : p.use) {
      auto [name, extras] = split_extras(u.name, modules);
      if (!modules.contains(name)) return unknown(std::format("profile {} uses", p.name), u.name);
      u.name = std::move(name);
      u.extras = extras;
    }
  }
  return {};
}

Result<ResolvedModule> Evaluator::resolve(const std::string& name, const Value& param_overrides) {
  PhaseScope eval(host_, Phase::Eval);
  Value overrides = param_overrides.is_nil() ? Value{Map{}} : param_overrides;
  auto v = host_.call("resolve_module", {name, overrides});
  if (!v) return std::unexpected(v.error());
  return ResolvedModule::from_value(*v);
}

}  // namespace repro
