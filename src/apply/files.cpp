// File-like executors: files (link/copy/create), env (environment.d, one file
// per module, research R13) and udev rules. Writes are atomic (research R20).
#include <format>

#include "apply/checks.hpp"
#include "apply/executors.hpp"
#include "util/sha256.hpp"
#include "util/strings.hpp"

namespace repro {

// Inserts or replaces the `# BEGIN repro` … `# END repro` block, keeping the
// rest of the file (and its mode) as the user wrote it.
Result<StepOutcome> apply_block(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<BlockDesired>(s.desired);
  const auto text = ctx.fs.read(d.physical);
  const FileStat st = ctx.fs.stat(d.physical);
  const std::string next = render_locale_gen(parse_locale_gen(text.value_or("")), d.lines);
  if (auto r = ctx.fs.mkdirs(dirname(d.physical), 0755); !r) return std::unexpected(r.error());
  if (auto r = ctx.fs.write_atomic(d.physical, next, st.type == FileType::File ? st.mode : 0644); !r) {
    return std::unexpected(r.error());
  }
  StepOutcome out;
  out.detail["block"] = d.purpose;
  return out;
}

ManagedFile managed_row(const Step& s) {
  const auto& d = std::get<FileDesired>(s.desired);
  ManagedFile m;
  m.path = d.logical;
  m.kind = d.kind;
  m.module = s.module;
  if (d.kind == "link") m.link_target = d.link_target;
  else m.digest = sha256_hex(d.content);
  return m;
}

Result<StepOutcome> apply_file(const Step& s, ApplyContext& ctx) {
  const auto& d = std::get<FileDesired>(s.desired);
  StepOutcome out;
  if (auto r = ctx.fs.mkdirs(dirname(d.physical), 0755); !r) return std::unexpected(r.error());
  const FileStat cur = ctx.fs.lstat(d.physical);
  if (cur.type == FileType::Dir && !(s.adopting && d.kind == "link")) {
    return fail(ec::kStep, std::format("{} is a directory", ctx.paths.display(d.logical)),
                "move it away or choose another destination");
  }
  if (s.adopting && cur.exists()) {
    out.backup = adoption_backup_path(d.physical, ctx.stamp);
    if (auto r = ctx.fs.rename(d.physical, out.backup); !r) return std::unexpected(r.error());
    out.adopted = true;
    out.detail["backup"] = out.backup;
  }
  if (d.kind == "link") {
    if (auto r = ctx.fs.symlink_atomic(d.link_target, d.physical); !r) return std::unexpected(r.error());
    out.detail["link_target"] = d.link_target;
  } else {
    // A regular file replaced by content: a symlink at dest is replaced, not followed.
    const mode_t mode = d.mode.value_or(cur.type == FileType::File && !s.adopting ? cur.mode : 0644);
    if (auto r = ctx.fs.write_atomic(d.physical, d.content, mode); !r) return std::unexpected(r.error());
    out.detail["digest"] = sha256_hex(d.content);
  }
  out.managed = managed_row(s);
  return out;
}

}  // namespace repro
