#include "plan/resolver.hpp"

#include <algorithm>
#include <format>

#include "util/strings.hpp"

namespace repd {

Resolver::Resolver(const std::map<std::string, ModuleMeta>& modules, ResolverInputs in)
    : modules_(modules), in_(std::move(in)) {}

std::vector<const ModuleMeta*> Resolver::candidates(const std::string& iface) const {
  std::vector<const ModuleMeta*> out;
  for (const auto& [name, m] : modules_) {
    if (!m.is_interface() && m.provides == iface) out.push_back(&m);
  }
  std::ranges::sort(out, [](const ModuleMeta* a, const ModuleMeta* b) {
    return std::tie(a->priority, a->name) < std::tie(b->priority, b->name);
  });
  return out;
}

namespace {

std::string candidate_list(const std::vector<const ModuleMeta*>& cands) {
  std::vector<std::string> names;
  for (const auto* c : cands) names.push_back(c->name);
  return join(names, ", ");
}

}  // namespace

Result<std::string> Resolver::check_override(const std::string& iface, const std::string& provider,
                                             std::string_view origin) const {
  auto it = modules_.find(provider);
  if (it != modules_.end() && !it->second.is_interface() && it->second.provides == iface) {
    return provider;
  }
  const auto cands = candidate_list(candidates(iface));
  return fail(ec::kProvider,
              std::format("{} selects {} for interface {}, but {} does not provide it", origin,
                          provider, iface, provider),
              std::format("choose one of: {}", cands.empty() ? "(no providers)" : cands));
}

void Resolver::choose(const std::string& iface, const std::string& provider, std::string source) {
  resolved_[iface] = ResolvedProvider{iface, provider, std::move(source), false};
}

Result<ResolvedProvider> Resolver::resolve(const std::string& iface,
                                           const std::vector<std::string>& requirers) {
  if (auto it = resolved_.find(iface); it != resolved_.end()) return it->second;
  auto done = [&](std::string provider, std::string source, bool persist) {
    ResolvedProvider p{iface, std::move(provider), std::move(source), persist};
    resolved_[iface] = p;
    return p;
  };
  if (auto it = in_.cli.find(iface); it != in_.cli.end()) {
    auto p = check_override(iface, it->second, "--provider");
    if (!p) return std::unexpected(p.error());
    return done(*p, "cli", true);
  }
  if (auto it = in_.profile.find(iface); it != in_.profile.end()) {
    auto p = check_override(iface, it->second, "the profile");
    if (!p) return std::unexpected(p.error());
    return done(*p, "profile", false);
  }
  if (auto it = in_.pins.find(iface); it != in_.pins.end()) {
    auto p = check_override(iface, it->second, "rp.pin in repd.lua");
    if (!p) return std::unexpected(p.error());
    return done(*p, "pin", false);
  }
  if (!in_.reselect.contains(iface)) {
    if (auto it = in_.lock.find(iface); it != in_.lock.end()) {
      if (check_override(iface, it->second.provider, "lock")) {
        return done(it->second.provider, "lock", false);
      }
      warnings_.push_back(std::format(
          "repd.lock.lua records {} for {}, which no longer provides it; choosing again",
          it->second.provider, iface));
    }
  }
  const auto cands = candidates(iface);
  const std::string req = requirers.empty() ? "" : " (required by " + join(requirers, ", ") + ")";
  if (cands.empty()) {
    return fail(ec::kNoProvider, std::format("interface {}{} has no providers", iface, req),
                std::format("add a module with provides = \"{}\"", iface));
  }
  if (cands.size() == 1) return done(cands.front()->name, "only-option", true);
  if (in_.interactive && in_.prompt) {
    auto choice = in_.prompt(iface, cands, requirers);
    if (!choice) return std::unexpected(choice.error());
    auto p = check_override(iface, *choice, "the prompt");
    if (!p) return std::unexpected(p.error());
    return done(*p, "prompt", true);
  }
  return fail(ec::kAmbiguous,
              std::format("interface {}{} has several providers and none is selected: {}", iface,
                          req, candidate_list(cands)),
              std::format("re-run interactively, or pass --provider {}={} (or pin it in repd.lua)",
                          iface, cands.front()->name));
}

}  // namespace repd
