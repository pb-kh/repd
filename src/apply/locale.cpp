// Locales (research R13): a managed block in /etc/locale.gen holding the
// sorted union of declared locales (existing block entries are kept), then a
// single locale-gen post-step; LANG= in /etc/locale.conf for a declared default.
#include <algorithm>
#include <format>
#include <set>

#include "apply/checks.hpp"
#include "apply/executors.hpp"
#include "platform/proc.hpp"
#include "util/sha256.hpp"
#include "util/strings.hpp"

namespace repd {

Result<StepOutcome> apply_locale(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<LocaleDesired>(s.desired);
  StepOutcome out;
  if (d.part == "locales") {
    const std::string logical = "/etc/locale.gen";
    const std::string physical = ctx.paths.physical(logical);
    const auto text = ctx.fs.read(physical);
    const LocaleGen gen = parse_locale_gen(text.value_or(""));
    std::set<std::string> block;
    for (const auto& l : gen.block) {
      if (!trim(l).empty() && !trim(l).starts_with('#')) block.insert(std::string(trim(l)));
    }
    block.insert(d.all_locales.begin(), d.all_locales.end());
    block.insert(d.locales.begin(), d.locales.end());
    const std::vector<std::string> lines(block.begin(), block.end());
    const std::string next = render_locale_gen(gen, lines);
    if (auto r = ctx.fs.mkdirs(dirname(physical), 0755); !r) return std::unexpected(r.error());
    if (auto r = ctx.fs.write_atomic(physical, next, 0644); !r) return std::unexpected(r.error());
    std::string block_text;
    for (const auto& l : lines) block_text += l + "\n";
    out.managed = ManagedFile{logical, "locale-block", sha256_hex(block_text), std::nullopt, s.module};
    List l;
    for (const auto& x : lines) l.emplace_back(x);
    out.detail["block"] = std::move(l);
    return out;
  }
  const std::string physical = ctx.paths.physical("/etc/locale.conf");
  const auto text = ctx.fs.read(physical);
  std::string next;
  bool replaced = false;
  for (const auto& line : split(text.value_or(""), '\n')) {
    if (trim(line).starts_with("LANG=")) {
      if (!replaced) next += "LANG=" + d.default_locale + "\n";
      replaced = true;
      continue;
    }
    next += line + "\n";
  }
  if (!replaced) next += "LANG=" + d.default_locale + "\n";
  if (auto r = ctx.fs.mkdirs(dirname(physical), 0755); !r) return std::unexpected(r.error());
  if (auto r = ctx.fs.write_atomic(physical, next, 0644); !r) return std::unexpected(r.error());
  out.detail["LANG"] = d.default_locale;
  return out;
}

Result<StepOutcome> apply_post(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<PostDesired>(s.desired);
  const std::vector<std::string> argv = d.what == "udev-reload"
                                            ? std::vector<std::string>{"udevadm", "control", "--reload"}
                                            : std::vector<std::string>{"locale-gen"};
  auto r = run_process(argv);
  if (!r) return std::unexpected(r.error());
  const std::string cmd = join(argv, " ");
  if (ctx.log != nullptr) ctx.log->append(std::format("$ {}\n{}{}", cmd, tail(r->out, 2048), tail(r->err, 2048)));
  if (r->code != 0) {
    return fail(ec::kStep, std::format("{} exited with status {}", cmd, r->code),
                std::format("run `{}` by hand to see the full error", cmd));
  }
  StepOutcome out;
  out.detail["command"] = cmd;
  return out;
}

}  // namespace repd
