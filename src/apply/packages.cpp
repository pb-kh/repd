// Package installs through each entry's backend (FR-020); repd records what it
// installed (managed_packages) but never removes packages in v1. Installs never
// refresh the package databases on their own: a refresh without an upgrade is a
// partial upgrade, which Arch does not support. `repd sync --upgrade` plans a
// full upgrade per backend before its installs instead.
#include <format>

#include "apply/executors.hpp"
#include "util/strings.hpp"

namespace repd {

Result<StepOutcome> apply_packages(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<PackagesDesired>(s.desired);
  Backend* b = ctx.backends ? ctx.backends->find("packages", d.backend) : nullptr;
  if (b == nullptr) return fail(ec::kNoBackend, std::format("packages backend {} is not available", d.backend), "");
  if (auto r = b->install(d.missing); !r) return std::unexpected(r.error());
  StepOutcome out;
  out.packages = d.missing;
  out.backend = b->info().name;
  List names;
  for (const auto& n : d.missing) names.emplace_back(n);
  out.detail["installed"] = std::move(names);
  out.detail["backend"] = b->info().name;
  return out;
}

Result<StepOutcome> apply_upgrade(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<UpgradeDesired>(s.desired);
  Backend* b = ctx.backends ? ctx.backends->find("packages", d.backend) : nullptr;
  if (b == nullptr) return fail(ec::kNoBackend, std::format("packages backend {} is not available", d.backend), "");
  StepOutcome out;
  out.detail["backend"] = b->info().name;
  if (d.targeted) {
    if (auto r = b->upgrade_packages(d.installed); !r) return std::unexpected(r.error());
    List names;
    for (const auto& n : d.installed) names.emplace_back(n);
    out.detail["upgraded"] = Value{std::move(names)};
    return out;
  }
  if (auto r = b->upgrade(); !r) return std::unexpected(r.error());
  out.detail["upgraded"] = true;
  return out;
}

}  // namespace repd
