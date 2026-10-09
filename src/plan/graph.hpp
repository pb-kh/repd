// Scope selection (data-model.md "Scope selection", research R10): closure
// over requires (optional ones only when asked for) with interfaces replaced by
// resolved providers, integrations added once everything they integrate is in,
// DFS cycle detection, and Kahn topological order with lexicographic tie-break.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "eval/declaration.hpp"
#include "plan/resolver.hpp"
#include "util/errors.hpp"

namespace repro {

// An integration (rp.module { integrates = { ... } }) touched by this selection.
struct IntegrationStatus {
  std::string name;
  bool active = false;                // being synced
  bool held = false;                  // everything is there, but auto_sync = false
  std::vector<std::string> missing;   // integrates entries not being synced
};

struct Closure {
  std::vector<std::string> order;                          // dependencies first
  std::map<std::string, ResolvedProvider> providers;       // per interface
  std::map<std::string, std::vector<std::string>> deps;    // module -> modules
  std::vector<std::string> named;                          // roots (and "+" extras), as modules
  // Synced module -> its optional requires that are not synced.
  std::map<std::string, std::vector<std::string>> optional;
  std::vector<IntegrationStatus> integrations;             // by name
};

// "dev/rust+" -> {"dev/rust", true}; an existing module named "x+" wins.
std::pair<std::string, bool> split_extras(const std::string& name, const std::map<std::string, ModuleMeta>& modules);

// Roots may end in "+" (with their optional requires). `root_source` labels
// interfaces satisfied by a provider selected directly ("cli" for positional
// modules, "profile" for a profile's use list). `auto_sync` holds a profile's
// overrides of modules' auto_sync.
Result<Closure> select_scope(const std::map<std::string, ModuleMeta>& modules,
                             const std::vector<std::string>& roots, Resolver& resolver,
                             const std::string& root_source = "cli",
                             const std::map<std::string, bool>& auto_sync = {});

// Topological order (dependencies first, ties by name) or E_CYCLE with the chain.
Result<std::vector<std::string>> topo_order(const std::map<std::string, std::vector<std::string>>& deps);

}  // namespace repro
