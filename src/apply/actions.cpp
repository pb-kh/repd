// Imperative escape hatch (FR-019): action run(ctx) in the APPLY phase.
#include "apply/executors.hpp"

namespace repd {

Result<StepOutcome> apply_action(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<ActionDesired>(s.desired);
  if (ctx.host == nullptr) return fail(ec::kStep, "no Lua host", "this is a bug in repd");
  PhaseScope phase(*ctx.host, Phase::Apply);
  auto r = ctx.host->call("action", {s.module, d.name, "run", ctx.action_info});
  if (!r) return std::unexpected(r.error());
  return StepOutcome{};
}

}  // namespace repd
