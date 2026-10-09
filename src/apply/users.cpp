// System users (data-model.md "UserEntry"): create with useradd, or add the
// missing supplementary groups / shell with usermod. Additive only: never
// deletes users or removes group membership.
#include <format>

#include "apply/executors.hpp"
#include "platform/proc.hpp"
#include "util/strings.hpp"

namespace repro {

namespace {

Status run_tool(const std::vector<std::string>& argv, ApplyContext& ctx, StepOutcome& out) {
  auto r = run_process(argv);
  if (!r) return std::unexpected(r.error());
  const std::string cmd = join(argv, " ");
  if (ctx.log != nullptr) ctx.log->append(std::format("$ {}\n{}{}", cmd, tail(r->out, 2048), tail(r->err, 2048)));
  out.detail["command"] = cmd;
  if (r->code != 0) {
    return fail(ec::kStep, std::format("{} exited with status {}: {}", argv[0], r->code, trim(tail(r->err, 300))),
                std::format("run `{}` by hand to see the full error", cmd));
  }
  return {};
}

}  // namespace

Result<StepOutcome> apply_user(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<UserDesired>(s.desired);
  StepOutcome out;
  if (!d.exists) {
    std::vector<std::string> argv = {"useradd", "-m"};
    if (d.entry.shell) argv.insert(argv.end(), {"-s", *d.entry.shell});
    if (!d.entry.groups.empty()) argv.insert(argv.end(), {"-G", join(d.entry.groups, ",")});
    argv.push_back(d.entry.name);
    if (auto r = run_tool(argv, ctx, out); !r) return std::unexpected(r.error());
    return out;
  }
  if (d.shell_differs) {
    if (auto r = run_tool({"usermod", "-s", *d.entry.shell, d.entry.name}, ctx, out); !r) {
      return std::unexpected(r.error());
    }
  }
  if (!d.missing_groups.empty()) {
    if (auto r = run_tool({"usermod", "-a", "-G", join(d.missing_groups, ","), d.entry.name}, ctx, out); !r) {
      return std::unexpected(r.error());
    }
  }
  return out;
}

}  // namespace repro
