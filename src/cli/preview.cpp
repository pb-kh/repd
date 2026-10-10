#include "cli/preview.hpp"

#include <format>
#include <map>

namespace repd {

namespace {

std::string plural(int n, std::string_view one, std::string_view many) {
  return std::format("{} {}", n, n == 1 ? one : many);
}

bool shown(const Step& s) {
  return s.check == CheckResult::NeedsChange || s.check == CheckResult::Blocked ||
         s.check == CheckResult::OtherMode;
}

std::string module_status(const Plan& plan, const std::string& module) {
  int changes = 0, blocked = 0, other_pass = 0;
  for (const auto* s : plan.steps_of(module)) {
    if (s->cls == StepClass::Post) continue;
    if (s->check == CheckResult::NeedsChange) ++changes;
    if (s->check == CheckResult::Blocked) ++blocked;
    if (s->check == CheckResult::OtherMode) ++other_pass;
  }
  std::vector<std::string> parts;
  if (changes > 0) parts.push_back(plural(changes, "change", "changes"));
  if (blocked > 0) parts.push_back(std::format("{} blocked", blocked));
  if (other_pass > 0) parts.push_back(std::format("{} for {} pass", other_pass, to_string(other(plan.mode))));
  if (parts.empty()) return "up to date";
  std::string out = parts[0];
  for (std::size_t i = 1; i < parts.size(); ++i) out += ", " + parts[i];
  return out;
}

}  // namespace

std::string other_pass_hint(Mode mode) {
  return mode == Mode::User ? "sudo repd sync -s" : "repd sync -u";
}

std::string render_plan(const Plan& plan, const Style& st) {
  std::string out = std::format("repd plan — mode: {} — profile: {}\n", to_string(plan.mode),
                                plan.profile.value_or("(none)"));
  if (!plan.providers.empty()) {
    out += "providers:\n";
    for (const auto& [iface, p] : plan.providers) {
      out += std::format("  {} = {} ({})\n", iface, p.provider, p.source);
    }
  }
  if (!plan.backends.empty()) {
    out += "backends:\n";
    for (const auto& [key, b] : plan.backends) {
      const std::string origin = b.origin == "repo" ? "repo ⚠ reduced trust" : b.origin;
      out += std::format("  {} = {} {} ({}, {})\n", b.ns, b.name, b.version, origin, b.how);
    }
  }
  out += std::format("modules ({}):\n", plan.selected.size());
  for (const auto& module : plan.selected) {
    out += std::format("  {:<20} {}\n", module, module_status(plan, module));
    for (const auto* s : plan.steps_of(module)) {
      if (!shown(*s)) continue;
      std::string marker;
      switch (s->check) {
        case CheckResult::NeedsChange: marker = s->is_update ? st.yellow("~") : st.green("+"); break;
        case CheckResult::Blocked: marker = st.red("!"); break;
        default: marker = st.dim("·"); break;
      }
      std::string line = std::format("    {} {:<7} {}", marker, to_string(s->cls), s->summary);
      if (s->check == CheckResult::OtherMode) line += std::format("  [{} pass]", to_string(s->privilege));
      out += line + "\n";
    }
  }
  // Optional requires not synced, with whoever offers them.
  std::map<std::string, std::vector<std::string>> offered;
  for (const auto& [module, opts] : plan.optional) {
    for (const auto& o : opts) offered[o].push_back(module);
  }
  if (!offered.empty()) {
    out += "optional, not synced (name one, or add \"+\" to the module offering it):\n";
    for (const auto& [o, by] : offered) {
      std::string list;
      for (const auto& b : by) list += (list.empty() ? "" : ", ") + b;
      out += std::format("  {:<20} {}\n", o, st.dim("offered by " + list));
    }
  }
  if (!plan.integrations.empty()) {
    out += "integrations:\n";
    for (const auto& i : plan.integrations) {
      std::string state = "active";
      if (i.held) {
        state = st.dim("auto_sync = false: name it to sync");
      } else if (!i.active) {
        std::string list;
        for (const auto& m : i.missing) list += (list.empty() ? "" : ", ") + m;
        state = st.dim("inactive, needs " + list);
      }
      out += std::format("  {:<20} {}\n", i.name, state);
    }
  }
  const int changes = plan.changes();
  const int blocked = plan.blocked();
  const int pending = plan.pending_other();
  if (changes == 0 && blocked == 0 && pending == 0) {
    out += "summary: up to date\n";
    return out;
  }
  out += std::format("summary: {} in this pass", plural(changes, "change", "changes"));
  if (blocked > 0) out += std::format(", {} blocked", blocked);
  if (pending > 0) {
    out += std::format(", {} pending for {} pass (run: {})", pending, to_string(other(plan.mode)),
                       other_pass_hint(plan.mode));
  }
  return out + "\n";
}

std::string render_apply_summary(const Plan& plan, const ApplyResult& r) {
  std::string out;
  if (r.exit_code == kExitOk) {
    if (r.counts.applied == 0) {
      out = "up to date";
    } else {
      out = std::format("done: {} applied", r.counts.applied);
      if (r.adopted > 0) out += std::format(" ({} adopted, backups kept)", r.adopted);
    }
    if (const int p = plan.pending_other(); p > 0) {
      out += std::format("; {} pending for {} pass (run: {})", p, to_string(other(plan.mode)),
                         other_pass_hint(plan.mode));
    }
    return out + "\n";
  }
  out = std::format("stopped: {} applied, {} failed\n", r.counts.applied, r.counts.failed);
  const std::string reason = r.error ? r.error->message : "failed";
  std::string_view why = reason;
  if (why.starts_with(r.failed_step + ": ")) why.remove_prefix(r.failed_step.size() + 2);
  out += std::format("failed: {}: {}\n", r.failed_step, why);
  return out;
}

}  // namespace repd
