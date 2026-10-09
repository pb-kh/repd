// Apply engine (research R15, data-model.md "Step result lifecycle"): runs the
// plan's this-pass steps in order, re-checking each immediately before
// mutating (TOCTOU), committing the journal row + ledger updates in one
// transaction per step, and stopping at the first failure.
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "apply/checks.hpp"
#include "apply/executors.hpp"
#include "plan/plan.hpp"
#include "state/ledger.hpp"

namespace repro {

struct ApplyResult {
  int exit_code = kExitOk;
  RunCounts counts;
  std::int64_t run_id = 0;
  std::string failed_step;
  std::optional<Error> error;
  int adopted = 0;
};

using LineSink = std::function<void(std::string_view)>;

Result<StepOutcome> apply_step(const Step& s, ApplyContext& ctx);

ApplyResult apply_plan(Plan& plan, ApplyContext& actx, CheckContext& cctx, Ledger& ledger,
                       const std::string& argv, const LineSink& out);

}  // namespace repro
