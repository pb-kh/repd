// Plan preview and result summary (contracts/cli.md "Preview / plan output
// format"). Byte-stable: no timestamps, PIDs, run ids or durations (SC-003).
#pragma once

#include <string>

#include "apply/engine.hpp"
#include "cli/output.hpp"
#include "plan/plan.hpp"

namespace repd {

std::string render_plan(const Plan& plan, const Style& style);
std::string other_pass_hint(Mode mode);  // command to run the other pass
std::string render_apply_summary(const Plan& plan, const ApplyResult& r);

}  // namespace repd
