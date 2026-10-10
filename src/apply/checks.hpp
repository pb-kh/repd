// Satisfied-checks (research R10, R14): every local and backend step is
// checked on every run (this is what restores drift); actions are skipped
// when the module fingerprint is unchanged. Steps of the other pass are only
// probed, to count what that pass still has to do.
#pragma once

#include <map>
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

struct CheckContext {
  CheckContext(const Paths& p, const Fs& f) : paths(p), fs(f) {}

  const Paths& paths;
  const Fs& fs;
  Mode mode = Mode::User;
  const Ledger* ledger = nullptr;  // this mode's ownership ledger, if a store exists
  bool adopt = false;              // --adopt
  BackendRegistry* backends = nullptr;
  LuaHost* host = nullptr;
  Value action_info;  // {mode, home, user, providers} for action ctx
  std::map<std::string, ModuleState> states;        // this mode
  std::map<std::string, ModuleState> other_states;  // other mode, if readable
  std::map<std::string, std::string> fingerprints;
  std::set<std::string> refresh;
  std::map<std::string, std::map<std::string, bool>> installed_cache;  // backend -> name -> bool
};

// Runs the check for one step and fills check/summary/flags.
Status check_step(Step& step, CheckContext& ctx);
// Checks all steps; batches package queries per backend; resolves post-steps last.
Status check_all(std::vector<Step>& steps, CheckContext& ctx);

// Shared file-state helpers (also used by the executors).
struct LocaleGen {
  std::vector<std::string> before;  // lines before the managed block (or all lines)
  std::vector<std::string> block;   // lines inside # BEGIN repd / # END repd
  std::vector<std::string> after;
  bool has_block = false;
  std::set<std::string> active;     // every uncommented, non-empty line
};
LocaleGen parse_locale_gen(const std::string& text);
std::string render_locale_gen(const LocaleGen& gen, const std::vector<std::string>& block);
std::optional<std::string> locale_conf_lang(const std::string& text);

}  // namespace repd
