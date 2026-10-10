// Step synthesis (data-model.md §2 "Step", research R12, R13): turns resolved
// module declarations into ordered steps with privileges and desired state.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <repro/config.hpp>

#include "eval/declaration.hpp"
#include "plan/env_targets.hpp"
#include "plan/step.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"
#include "util/errors.hpp"

namespace repro {

struct BuildContext {
  const Paths& paths;
  const Fs& fs;
  const ConfigRepository& repo;
  std::map<std::string, Mode> package_backends;  // packages backend name -> privilege
  std::vector<std::string> all_locales;          // union over the selected scope
  EnvTargets env_targets;                        // where env / system_env are written
  std::string env_hook_owner;                    // module that owns the sh profile block
  std::string default_package_backend;  // rp.option("packages.default_backend"), "" if unset
};

// A `packages` entry: "yay:neovim" names its backend; "neovim" uses the default
// backend from rp.option("packages.default_backend"), if any (backend "" otherwise).
struct PackageRef {
  std::string backend;
  std::string name;
};
PackageRef split_package(const std::string& entry, const std::string& default_backend);
// The default packages backend: the option's value, "" when unset ("auto").
std::string default_package_backend(const std::string& option);
// E_CONFIG for an unprefixed package without a default backend.
Error package_without_backend(const std::string& who, const std::string& entry,
                               const std::vector<std::string>& available);

// Steps of one module in class order, then key order.
Result<std::vector<Step>> build_module_steps(const ResolvedModule& m, const BuildContext& ctx);

// One packages step per backend instead of one per module. Walking `modules` in
// apply order (each with its steps from build_module_steps), a module's install
// joins the backend's current batch, which runs at the batch's first module.
// It starts a new batch instead when it depends (transitively, via `deps`) on a
// module at or after that first module with other steps in the same pass,
// since those must run before its packages are installed (`extra` adds steps
// planned outside build_module_steps, e.g. upgrades). A package declared twice
// is installed once and owned by the first module that declares it.
void batch_packages(std::vector<std::pair<std::string, std::vector<Step>>>& modules,
                    const std::map<std::string, std::vector<std::string>>& deps,
                    const std::map<std::string, std::vector<Step>>& extra = {});

// Same env var in two modules of one scope, or two different locale defaults -> E_CONFLICT.
Status check_conflicts(const std::vector<const ResolvedModule*>& modules);
std::vector<std::string> locale_union(const std::vector<const ResolvedModule*>& modules);

// Inserts udev-reload / locale-gen after the last contributing step.
void add_post_steps(std::vector<Step>& steps);

// First module (in order) with user env, when the sh target is active; "" otherwise.
std::string env_hook_owner(const std::vector<const ResolvedModule*>& modules, const EnvTargets& t);

}  // namespace repro
