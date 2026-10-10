// Step executors (APPLY phase). Each mutates exactly one step's target and
// returns what the journal and ledgers need; the engine commits both in one
// transaction per step.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "eval/host.hpp"
#include "plan/step.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"
#include "plugins/backends.hpp"
#include "state/ledger.hpp"
#include "util/errors.hpp"
#include "util/value.hpp"

namespace repd {

struct ApplyContext {
  ApplyContext(const Paths& p, Fs& f) : paths(p), fs(f) {}

  const Paths& paths;
  Fs& fs;
  Mode mode = Mode::User;
  BackendRegistry* backends = nullptr;
  LuaHost* host = nullptr;
  Value action_info;
  std::string stamp;           // UTC, compact, for adoption backups
  std::string* log = nullptr;  // journal detail of the running step
};

struct StepOutcome {
  bool adopted = false;
  std::string backup;
  std::optional<ManagedFile> managed;      // ownership ledger row to upsert
  std::vector<std::string> packages;       // installed by this step
  std::string backend;
  Map detail;                              // canon-encoded into steps.detail
};

Result<StepOutcome> apply_file(const Step& s, ApplyContext& ctx);  // files, env, udev
Result<StepOutcome> apply_block(const Step& s, ApplyContext& ctx);  // managed block in a foreign file
Result<StepOutcome> apply_packages(const Step& s, ApplyContext& ctx);
Result<StepOutcome> apply_upgrade(const Step& s, ApplyContext& ctx);
Result<StepOutcome> apply_service(const Step& s, ApplyContext& ctx);
Result<StepOutcome> apply_user(const Step& s, ApplyContext& ctx);
Result<StepOutcome> apply_locale(const Step& s, ApplyContext& ctx);
Result<StepOutcome> apply_action(const Step& s, ApplyContext& ctx);
Result<StepOutcome> apply_post(const Step& s, ApplyContext& ctx);

Result<StepOutcome> apply_step(const Step& s, ApplyContext& ctx);

// Ledger row describing the desired (now current) state of a file step.
ManagedFile managed_row(const Step& s);

// Adoption guard (research R14).
std::string adoption_backup_path(const std::string& physical, const std::string& stamp);
Error blocked_error(const Step& s, const Paths& paths);

}  // namespace repd
