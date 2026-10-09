// The plan (data-model.md §2 "Run"): the full, ordered, checked list of steps
// for one mode, plus the decisions that produced it. Input to preview and apply.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "eval/declaration.hpp"
#include "plan/graph.hpp"
#include "plan/resolver.hpp"
#include "plan/step.hpp"
#include "platform/paths.hpp"

namespace repro {

struct BackendChoice {
  std::string ns;
  std::string name;
  std::string version;
  std::string origin;  // system|user|repo
  std::string how;     // auto|option (services); default|prefix (packages)
  Mode privilege = Mode::System;
};

struct Plan {
  Mode mode = Mode::User;
  std::optional<std::string> profile;
  std::vector<std::string> requested;
  std::vector<std::string> selected;  // topological order
  std::vector<std::string> named;     // asked for directly (roots and "+" extras)
  std::map<std::string, std::vector<std::string>> optional;  // module -> optional requires not synced
  std::vector<IntegrationStatus> integrations;
  std::map<std::string, ResolvedProvider> providers;
  std::map<std::string, BackendChoice> backends;  // "packages:<name>" (one per used backend), "services"
  std::string package_default;                    // rp.option("packages.backend"), "" if unset
  std::map<std::string, ResolvedModule> modules;
  std::map<std::string, std::string> fingerprints;
  std::vector<Step> steps;  // grouped by module in `selected` order
  std::set<std::string> refresh;

  int count(CheckResult r) const;
  int changes() const { return count(CheckResult::NeedsChange); }
  int pending_other() const { return count(CheckResult::OtherMode); }
  int blocked() const { return count(CheckResult::Blocked); }
  std::vector<const Step*> steps_of(const std::string& module) const;
};

}  // namespace repro
