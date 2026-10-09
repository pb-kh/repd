// Journal and ownership-ledger repository over the per-mode store
// (data-model.md §3.1, FR-012, FR-013, FR-016).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "state/db.hpp"
#include "util/errors.hpp"

namespace repro {

struct ManagedFile {
  std::string path;  // logical absolute path
  std::string kind;  // link|copy|create|env|udev|locale-block
  std::optional<std::string> digest;
  std::optional<std::string> link_target;
  std::string module;
};

struct ModuleState {
  std::string module;
  std::string fingerprint;
  std::string status;  // applied|failed|pending
  std::int64_t last_run_id = 0;
  std::string updated_at;
};

struct RunCounts {
  int applied = 0;
  int skipped = 0;
  int failed = 0;
  int pending_other = 0;
};

struct RunRow {
  std::int64_t id = 0;
  std::string started_at;
  std::optional<std::string> finished_at;
  std::string mode;
  std::optional<std::string> profile;
  std::string argv;
  std::string repro_version;
  std::optional<std::string> outcome;
  std::optional<std::int64_t> exit_code;
  RunCounts counts;
};

struct StepRow {
  std::int64_t run_id = 0;
  int seq = 0;
  std::string module;
  std::string step_id;
  std::string cls;
  bool imperative = false;
  std::string status;  // ok|skip|fail|interrupted|blocked
  std::string started_at;
  std::optional<std::string> finished_at;
  std::optional<std::string> error;
  std::optional<std::string> detail;
  bool adopted = false;
};

class Ledger {
 public:
  explicit Ledger(Db& db) : db_(db) {}

  // Runs fn inside BEGIN IMMEDIATE / COMMIT; rolls back when fn fails.
  Status transaction(const std::function<Status()>& fn);

  Result<std::int64_t> insert_run(std::string_view mode, const std::optional<std::string>& profile,
                                  std::string_view argv, std::string_view version);
  Status finish_run(std::int64_t run_id, std::string_view outcome, int exit_code,
                    const RunCounts& counts);
  Status set_run_providers(std::int64_t run_id,
                           const std::map<std::string, std::pair<std::string, std::string>>& p);
  Status set_run_module(std::int64_t run_id, std::string_view module,
                        std::string_view fingerprint, std::string_view status);
  Status append_step(const StepRow& row);
  Status update_module_state(std::string_view module, std::string_view fingerprint,
                             std::string_view status, std::int64_t run_id);
  Status upsert_managed_file(const ManagedFile& f);
  Status insert_managed_packages(std::string_view backend, const std::vector<std::string>& names,
                                 std::string_view module, std::int64_t run_id);

  std::optional<ManagedFile> managed_file(const std::string& path) const;
  std::map<std::string, ModuleState> module_states() const;
  std::vector<RunRow> recent_runs(int limit) const;
  std::optional<RunRow> run(std::int64_t id) const;
  std::map<std::string, std::pair<std::string, std::string>> run_providers(std::int64_t id) const;
  std::vector<StepRow> steps(std::int64_t run_id) const;
  std::optional<std::int64_t> last_run_touching(std::string_view module) const;

 private:
  Db& db_;
};

}  // namespace repro
