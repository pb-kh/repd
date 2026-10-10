// Service state through the selected services backend (FR-020).
#include "apply/executors.hpp"

namespace repd {

Result<StepOutcome> apply_service(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<ServiceDesired>(s.desired);
  Backend* b = ctx.backends ? ctx.backends->selected("services") : nullptr;
  if (b == nullptr) return fail(ec::kNoBackend, "no services backend selected", "");
  StepOutcome out;
  List ops;
  for (const auto& op : d.ops) {
    if (auto r = b->service(op, d.entry.unit, d.entry.scope); !r) return std::unexpected(r.error());
    ops.emplace_back(op);
  }
  out.detail["ops"] = std::move(ops);
  out.detail["backend"] = b->info().name;
  return out;
}

}  // namespace repd
