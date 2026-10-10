#include "apply/engine.hpp"

#include <format>
#include <map>

#include "apply/signals.hpp"
#include "platform/proc.hpp"
#include "util/strings.hpp"

namespace repd {

Result<StepOutcome> apply_step(const Step& s, ApplyContext& ctx) {
  switch (s.cls) {
    case StepClass::Env:
      if (std::holds_alternative<BlockDesired>(s.desired)) return apply_block(s, ctx);
      return apply_file(s, ctx);
    case StepClass::Files:
    case StepClass::Udev: return apply_file(s, ctx);
    case StepClass::Upgrade: return apply_upgrade(s, ctx);
    case StepClass::Packages: return apply_packages(s, ctx);
    case StepClass::Services: return apply_service(s, ctx);
    case StepClass::Users: return apply_user(s, ctx);
    case StepClass::Locale: return apply_locale(s, ctx);
    case StepClass::Action: return apply_action(s, ctx);
    case StepClass::Post: return apply_post(s, ctx);
  }
  return fail(ec::kStep, "unknown step class", "");
}

namespace {

std::string encode_detail(LuaHost* host, Map detail, const std::string& log) {
  if (!log.empty()) detail["output"] = tail(log, 4096);
  if (detail.empty()) return {};
  if (host == nullptr) return {};
  auto v = host->call("encode", {Value{std::move(detail)}});
  return v ? v->as_string() : std::string{};
}

}  // namespace

ApplyResult apply_plan(Plan& plan, ApplyContext& actx, CheckContext& cctx, Ledger& ledger,
                       const std::string& argv, const LineSink& out) {
  ApplyResult result;
  auto run = ledger.insert_run(to_string(plan.mode), plan.profile, argv, REPD_VERSION);
  if (!run) {
    result.exit_code = run.error().exit_code();
    result.error = run.error();
    return result;
  }
  result.run_id = *run;
  std::map<std::string, std::pair<std::string, std::string>> providers;
  for (const auto& [iface, p] : plan.providers) providers[iface] = {p.provider, p.source};
  (void)ledger.set_run_providers(*run, providers);

  int seq = 0;
  std::map<std::string, std::string> module_status;  // ok|failed|interrupted|blocked
  bool stop = false;
  std::string stamp = utc_now();
  actx.stamp = replace_all(replace_all(stamp, "-", ""), ":", "");

  auto record = [&](const Step& s, std::string_view status, const std::string& started,
                    const std::optional<std::string>& error, const StepOutcome* outcome,
                    const std::string& log) -> Status {
    StepRow row;
    row.run_id = *run;
    row.seq = ++seq;
    row.module = s.module;
    row.step_id = s.id;
    row.cls = std::string(to_string(s.cls));
    row.imperative = s.imperative;
    row.status = std::string(status);
    row.started_at = started;
    row.finished_at = utc_now();
    row.error = error;
    const std::string detail = encode_detail(actx.host, outcome ? outcome->detail : Map{}, log);
    if (!detail.empty()) row.detail = detail;
    row.adopted = outcome != nullptr && outcome->adopted;
    return ledger.transaction([&]() -> Status {
      if (auto r = ledger.append_step(row); !r) return r;
      if (outcome != nullptr && outcome->managed) {
        if (auto r = ledger.upsert_managed_file(*outcome->managed); !r) return r;
      }
      if (outcome != nullptr && !outcome->packages.empty()) {
        // A batch installs for several modules: record each package for the module declaring it.
        std::map<std::string, std::vector<std::string>> by_module;
        const auto* d = std::get_if<PackagesDesired>(&s.desired);
        for (const auto& n : outcome->packages) {
          std::string module = s.module;
          if (d != nullptr) {
            if (auto it = d->owners.find(n); it != d->owners.end()) module = it->second;
          }
          by_module[module].push_back(n);
        }
        for (const auto& [module, names] : by_module) {
          if (auto r = ledger.insert_managed_packages(outcome->backend, names, module, *run); !r)
            return r;
        }
      }
      return {};
    });
  };

  auto fail_with = [&](const Step& s, const Error& e, int exit_code) {
    result.exit_code = exit_code;
    result.failed_step = s.id;
    result.error = e;
    stop = true;
  };

  for (const auto& module : plan.selected) {
    if (stop) break;
    std::vector<Step*> steps;
    for (auto& s : plan.steps) {
      if (s.module == module) steps.push_back(&s);
    }
    // Adoption guard: the run stops before the module's first mutation.
    for (Step* s : steps) {
      if (s->check != CheckResult::Blocked) continue;
      const Error e = blocked_error(*s, actx.paths);
      (void)record(*s, "blocked", utc_now(), e.message, nullptr, "");
      out(std::format("{:<4}  {}", "FAIL", s->id));
      module_status[module] = "blocked";
      ++result.counts.failed;
      fail_with(*s, e, kExitApplyFailed);
      break;
    }
    if (stop) break;
    module_status[module] = "ok";
    for (Step* s : steps) {
      if (interrupted()) {
        module_status[module] = "interrupted";
        fail_with(*s, Error{std::string(ec::kInterrupted), "interrupted by signal",
                            "run the same command again to continue; completed steps are kept"},
                  kExitInterrupted);
        break;
      }
      if (s->check == CheckResult::Satisfied || s->check == CheckResult::SkippedFp) {
        if (s->record_only && s->cls != StepClass::Locale) {
          (void)ledger.transaction([&]() { return ledger.upsert_managed_file(managed_row(*s)); });
        }
        ++result.counts.skipped;
        continue;
      }
      if (s->check != CheckResult::NeedsChange) continue;  // other pass
      const std::string started = utc_now();
      // TOCTOU guard: re-check right before mutating (actions/post-steps were just decided).
      if (s->cls != StepClass::Action && s->cls != StepClass::Post) {
        Step fresh = *s;
        if (auto r = check_step(fresh, cctx); r && fresh.check == CheckResult::Satisfied) {
          (void)record(*s, "skip", started, std::nullopt, nullptr, "");
          out(std::format("{:<4}  {}", "skip", s->id));
          ++result.counts.skipped;
          continue;
        } else if (r && fresh.check == CheckResult::NeedsChange) {
          fresh.adopting = fresh.adopting || s->adopting;
          *s = std::move(fresh);
        }
      }
      std::string log;
      actx.log = &log;
      if (actx.host != nullptr) actx.host->set_log_sink(&log);
      auto r = apply_step(*s, actx);
      if (actx.host != nullptr) actx.host->set_log_sink(nullptr);
      actx.log = nullptr;
      if (!r) {
        const Error& e = r.error();
        const bool was_interrupted = interrupted();
        (void)record(*s, was_interrupted ? "interrupted" : "fail", started, e.message, nullptr, log);
        out(std::format("{:<4}  {}", "FAIL", s->id));
        ++result.counts.failed;
        module_status[module] = was_interrupted ? "interrupted" : "failed";
        if (was_interrupted) {
          fail_with(*s, Error{std::string(ec::kInterrupted), "interrupted by signal",
                              "run the same command again to continue; completed steps are kept"},
                    kExitInterrupted);
        } else {
          fail_with(*s, Error{std::string(ec::kStep), std::format("{}: {}", s->id, e.message), e.hint},
                    kExitApplyFailed);
        }
        break;
      }
      if (auto w = record(*s, "ok", started, std::nullopt, &*r, log); !w) {
        fail_with(*s, w.error(), kExitApplyFailed);
        break;
      }
      if (r->adopted) ++result.adopted;
      out(std::format("{:<4}  {}", "ok", s->id));
      ++result.counts.applied;
    }
  }
  result.counts.pending_other = plan.pending_other();

  // Close the run: per-module outcome and the denormalised module_state.
  for (const auto& [module, status] : module_status) {
    const std::string fp = plan.fingerprints.contains(module) ? plan.fingerprints.at(module) : "";
    (void)ledger.set_run_module(*run, module, fp, status);
    const std::string state = status == "ok" ? "applied" : status == "blocked" ? "pending" : "failed";
    (void)ledger.update_module_state(module, fp, state, *run);
  }
  const std::string outcome = result.exit_code == kExitOk           ? "ok"
                              : result.exit_code == kExitInterrupted ? "interrupted"
                                                                     : "failed";
  (void)ledger.finish_run(*run, outcome, result.exit_code, result.counts);
  return result;
}

}  // namespace repd
