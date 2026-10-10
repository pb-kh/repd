// The run pipeline (plan.md Summary): discover -> evaluate (sandboxed) ->
// resolve (profile, closure, providers) -> plan (ordered, checked steps).
// Shared by sync, status, show and list.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <repd/config.hpp>

#include "apply/checks.hpp"
#include "eval/evaluate.hpp"
#include "eval/host.hpp"
#include "plan/lock.hpp"
#include "plan/plan.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"
#include "plugins/backends.hpp"
#include "state/db.hpp"
#include "state/ledger.hpp"

namespace repd {

struct GlobalOptions {
  std::optional<std::string> config;
  bool no_color = false;
  std::vector<std::string> options;  // --option KEY=VALUE, over rp.option() in repd.lua
};

class Session {
 public:
  static Result<std::unique_ptr<Session>> open(const GlobalOptions& g, Mode mode);

  // Root config, native + repo-local backends, all modules, cross-reference checks.
  Status load();

  Paths paths;
  RealFs fs;
  Mode mode;
  ConfigRepository repo;
  std::unique_ptr<LuaHost> host;
  std::unique_ptr<Evaluator> eval;
  BackendRegistry backends;
  RootConfig root;
  std::map<std::string, ModuleMeta> modules;

  std::string option(const std::string& key) const;
  std::vector<std::string> option_flags;  // --option KEY=VALUE
  // rp.packages.<op>(names) from Lua, routed per name like `packages` entries.
  Result<Value> dispatch_packages(const std::string& op, const List& args);
  std::string db_path(Mode m) const { return paths.state_dir(m) + "/repd.db"; }

 private:
  Session(Paths p, Mode m) : paths(std::move(p)), mode(m) {}
  // Validates plugin options from repd.lua and applies --option overrides.
  Status apply_options();
};

struct PlanRequest {
  std::vector<std::string> modules;          // positional
  std::optional<std::string> profile;        // --profile
  std::map<std::string, std::string> providers;  // --provider I=M
  std::set<std::string> reselect;
  std::set<std::string> refresh;
  bool adopt = false;
  bool interactive = false;
  bool upgrade = false;  // --upgrade: full upgrade of each packages backend in use
};

struct PlanOutput {
  Plan plan;
  LockFile lock;                 // as read from disk
  bool other_store_readable = false;
};

// ledger: this mode's ledger (nullptr when no store exists yet).
Result<PlanOutput> make_plan(Session& s, const PlanRequest& req, const Ledger* ledger);

// Check context for a plan (also used by apply for TOCTOU re-checks).
CheckContext make_check_context(Session& s, const Plan& plan, const PlanRequest& req,
                                const Ledger* ledger, bool* other_store_readable = nullptr);

// Profile selection + roots (data-model.md "Scope selection" step 1).
struct Scope {
  const Profile* profile = nullptr;
  std::vector<std::string> roots;
  std::string root_source;  // cli|profile
  std::map<std::string, bool> auto_sync;  // the profile's { "m", auto_sync = ... } overrides
};
Result<Scope> select_roots(const Session& s, const PlanRequest& req);

Result<std::map<std::string, std::string>> parse_provider_flags(const std::vector<std::string>& flags);

}  // namespace repd
