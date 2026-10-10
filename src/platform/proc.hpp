// Process spawning (research R20): argv vectors only, never a shell — except
// run_shell(), which exists solely for the documented ctx.exec() escape hatch.
#pragma once

#include <string>
#include <vector>

#include "util/errors.hpp"

namespace repd {

struct ProcResult {
  int code = 0;  // exit status, or 128+signal
  std::string out;
  std::string err;
};

struct ProcOptions {
  std::string stdin_data;
  bool inherit_output = false;  // stream to our stdout/stderr instead of capturing
  bool inherit_input = false;   // the child reads our stdin (stdin_data is ignored)
};

// Runs argv[0] via PATH lookup. Fails only if the program cannot be started.
Result<ProcResult> run_process(const std::vector<std::string>& argv, const ProcOptions& opts = {});
// `/bin/sh -c cmd` — actions only (contracts/lua-api.md ctx.exec).
Result<ProcResult> run_shell(const std::string& cmd, const ProcOptions& opts = {});

// Pid of the child currently running (0 if none); used to forward SIGINT/SIGTERM.
int current_child_pid();

// Last `max` bytes of `s` (output tails stored in the journal, ≤ 4 KiB).
std::string tail(const std::string& s, std::size_t max = 4096);

}  // namespace repd
