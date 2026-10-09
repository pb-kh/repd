#include "apply/checks.hpp"

#include <algorithm>
#include <format>

#include "platform/users.hpp"
#include "util/sha256.hpp"
#include "util/strings.hpp"

namespace repro {

namespace {

constexpr std::string_view kBlockBegin = "# BEGIN repro";
constexpr std::string_view kBlockEnd = "# END repro";

bool other_pass(const Step& s, const CheckContext& ctx) { return s.privilege != ctx.mode; }

// Collapses an unsatisfied other-pass result into OtherMode.
void finish_other(Step& s, const CheckContext& ctx) {
  if (other_pass(s, ctx) && s.check != CheckResult::Satisfied) {
    s.check = CheckResult::OtherMode;
    s.blocked_reason.clear();
    s.adopting = false;
  }
}

// ---- files / env / udev ------------------------------------------------------

struct OnDisk {
  FileStat st;
  std::string digest;  // regular files
  std::string link;    // symlinks
};

OnDisk inspect(const Fs& fs, const std::string& physical) {
  OnDisk d;
  d.st = fs.lstat(physical);
  if (d.st.type == FileType::Symlink) {
    if (auto t = fs.readlink(physical)) d.link = *t;
  } else if (d.st.type == FileType::File) {
    if (auto c = fs.read(physical)) d.digest = sha256_hex(*c);
  }
  return d;
}

std::string create_text(const FileDesired& d) {
  if (d.kind == "link") return std::format("link -> {}", d.source);
  if (d.kind == "copy") return std::format("copy from {}", d.source);
  return "create";
}

Status check_file(Step& s, CheckContext& ctx) {
  auto& d = std::get<FileDesired>(s.desired);
  const std::string disp = ctx.paths.display(d.logical);
  const bool is_env = s.cls == StepClass::Env;
  auto describe = [&](std::string_view what) {
    if (is_env) return std::format("{}  ({})", d.vars, disp);
    return std::format("{}  {}", disp, what);
  };
  if (!d.tree_root.empty()) {
    // Writing through a symlinked tree root would land in its target (often the
    // repo itself, when the directory used to be linked).
    const FileStat root = ctx.fs.lstat(ctx.paths.physical(d.tree_root));
    if (root.type == FileType::Symlink || root.type == FileType::File) {
      const std::string root_disp = ctx.paths.display(d.tree_root);
      const char* what = root.type == FileType::Symlink ? "is a symlink" : "is a file";
      s.check = CheckResult::Blocked;
      s.blocked_reason = std::format("{} {}", root_disp, what);
      s.summary = std::format("{}  {} {}, not a directory — remove it to copy the tree there", disp,
                              root_disp, what);
      return {};
    }
  }
  const OnDisk cur = inspect(ctx.fs, d.physical);
  const std::string want_digest = d.kind == "link" ? "" : sha256_hex(d.content);
  if (!cur.st.exists()) {
    s.check = CheckResult::NeedsChange;
    s.summary = describe(create_text(d));
    return {};
  }
  if (cur.st.type == FileType::Dir && d.kind == "link" && (d.adopt || ctx.adopt)) {
    s.check = CheckResult::NeedsChange;
    s.is_update = true;
    s.adopting = true;
    s.summary = describe(std::format("adopt existing directory (backup kept), then {}", create_text(d)));
    return {};
  }
  if (cur.st.type == FileType::Dir) {
    s.check = CheckResult::Blocked;
    s.blocked_reason = "is a directory";
    s.summary = d.kind == "link"
                    ? std::format("{}  is a directory — re-run with --adopt to move it aside, or set adopt = true", disp)
                    : std::format("{}  is a directory — move it away or choose another dest", disp);
    return {};
  }
  const bool content_ok = d.kind == "link" ? (cur.st.type == FileType::Symlink && cur.link == d.link_target)
                                           : (cur.st.type == FileType::File && cur.digest == want_digest);
  const bool mode_ok = d.kind == "link" || !d.mode || cur.st.mode == *d.mode;
  std::optional<ManagedFile> managed;
  if (ctx.ledger != nullptr && !other_pass(s, ctx)) managed = ctx.ledger->managed_file(d.logical);
  if (content_ok && mode_ok) {
    s.check = CheckResult::Satisfied;
    const std::string recorded = managed ? (d.kind == "link" ? managed->link_target.value_or("")
                                                             : managed->digest.value_or(""))
                                         : "";
    const std::string want = d.kind == "link" ? d.link_target : want_digest;
    s.record_only = !other_pass(s, ctx) && (!managed || recorded != want || managed->module != s.module);
    return {};
  }
  s.is_update = true;
  if (content_ok) {  // only the mode differs: the content is already ours
    s.check = CheckResult::NeedsChange;
    s.summary = describe("update (mode differs)");
    return {};
  }
  if (managed) {
    const bool on_disk_is_recorded =
        (managed->link_target && cur.st.type == FileType::Symlink && cur.link == *managed->link_target) ||
        (managed->digest && cur.st.type == FileType::File && cur.digest == *managed->digest);
    s.check = CheckResult::NeedsChange;
    if (on_disk_is_recorded) {
      s.summary = describe(d.kind == "link" ? std::format("relink -> {}", d.source) : "update (content differs)");
    } else {
      s.summary = describe("restore drift");
    }
    return {};
  }
  if (d.adopt || ctx.adopt) {
    s.check = CheckResult::NeedsChange;
    s.adopting = true;
    s.summary = describe(std::format("adopt existing file (backup kept), then {}", create_text(d)));
    return {};
  }
  s.check = CheckResult::Blocked;
  s.blocked_reason = "exists and is not managed by repro";
  s.summary = std::format("{}  exists and is not managed by repro — re-run with --adopt or set adopt = true", disp);
  return {};
}

// ---- managed blocks ------------------------------------------------------------

Status check_block(Step& s, CheckContext& ctx) {
  const auto& d = std::get<BlockDesired>(s.desired);
  const auto text = ctx.fs.read(d.physical);
  const LocaleGen gen = parse_locale_gen(text.value_or(""));
  s.is_update = text.has_value();
  s.check = gen.has_block && gen.block == d.lines ? CheckResult::Satisfied : CheckResult::NeedsChange;
  s.summary = std::format("{}  {} repro block ({})", ctx.paths.display(d.logical),
                          gen.has_block ? "update" : "add", d.purpose);
  return {};
}

// ---- upgrade -----------------------------------------------------------------

// Only planned for `repro sync --upgrade`: always a change in its own pass.
Status check_upgrade(Step& s, CheckContext& ctx) {
  auto& d = std::get<UpgradeDesired>(s.desired);
  s.is_update = true;
  if (!d.targeted) {
    s.check = CheckResult::NeedsChange;
    s.summary = std::format("{}: upgrade all installed packages", d.backend);
    return {};
  }
  // Only what is installed: the packages step installs the rest at its latest version.
  Backend* b = ctx.backends ? ctx.backends->find("packages", d.backend) : nullptr;
  if (b == nullptr) return fail(ec::kNoBackend, std::format("packages backend {} is not available", d.backend), "");
  auto& cache = ctx.installed_cache[b->info().name];
  std::vector<std::string> unknown;
  for (const auto& n : d.names) {
    if (!cache.contains(n)) unknown.push_back(n);
  }
  if (!unknown.empty()) {
    auto r = b->installed(unknown);
    if (!r) return std::unexpected(r.error());
    for (const auto& [n, ok] : *r) cache[n] = ok;
  }
  d.installed.clear();
  for (const auto& n : d.names) {
    if (cache[n]) d.installed.push_back(n);
  }
  s.check = d.installed.empty() ? CheckResult::Satisfied : CheckResult::NeedsChange;
  s.summary = std::format("{}: upgrade {}", d.backend, join(d.installed.empty() ? d.names : d.installed, ", "));
  return {};
}

// ---- packages ----------------------------------------------------------------

Status check_packages(Step& s, CheckContext& ctx) {
  auto& d = std::get<PackagesDesired>(s.desired);
  Backend* b = ctx.backends ? ctx.backends->find("packages", d.backend) : nullptr;
  if (b == nullptr) return fail(ec::kNoBackend, std::format("packages backend {} is not available", d.backend), "");
  auto& cache = ctx.installed_cache[b->info().name];
  std::vector<std::string> unknown;
  for (const auto& n : d.names) {
    if (!cache.contains(n)) unknown.push_back(n);
  }
  if (!unknown.empty()) {
    auto r = b->installed(unknown);
    if (!r) return std::unexpected(r.error());
    for (const auto& [n, ok] : *r) cache[n] = ok;
  }
  d.missing.clear();
  for (const auto& n : d.names) {
    if (!cache[n]) d.missing.push_back(n);
  }
  s.check = d.missing.empty() ? CheckResult::Satisfied : CheckResult::NeedsChange;
  s.summary = std::format("{}: install {}", d.backend, join(d.missing, ", "));
  return {};
}

// ---- services ----------------------------------------------------------------

Status check_service(Step& s, CheckContext& ctx) {
  auto& d = std::get<ServiceDesired>(s.desired);
  Backend* b = ctx.backends ? ctx.backends->selected("services") : nullptr;
  if (b == nullptr) return fail(ec::kNoBackend, "no services backend selected", "");
  auto st = b->state(d.entry.unit, d.entry.scope);
  if (!st) return std::unexpected(st.error());
  const ServiceState cur = st->value_or(ServiceState{});
  d.ops.clear();
  const bool want_enabled = d.entry.state == "enabled";
  if (want_enabled && !cur.enabled) d.ops.emplace_back("enable");
  if (!want_enabled && cur.enabled) d.ops.emplace_back("disable");
  if (d.entry.active == true && !cur.active) d.ops.emplace_back("start");
  if (d.entry.active == false && cur.active) d.ops.emplace_back("stop");
  s.check = d.ops.empty() ? CheckResult::Satisfied : CheckResult::NeedsChange;
  s.is_update = st->has_value();
  const std::string unit = d.entry.scope == "user" ? d.entry.unit + " (user)" : d.entry.unit;
  s.summary = std::format("{}  {}", unit, join(d.ops, ", "));
  return {};
}

// ---- users -------------------------------------------------------------------

Status check_user(Step& s, CheckContext& ctx) {
  auto& d = std::get<UserDesired>(s.desired);
  const auto pw = find_passwd_entry(ctx.fs, ctx.paths.physical("/etc/passwd"), d.entry.name);
  d.exists = pw.has_value();
  d.shell_differs = d.exists && d.entry.shell && pw->shell != *d.entry.shell;
  d.missing_groups.clear();
  const auto have = d.exists ? member_groups(ctx.fs, ctx.paths.physical("/etc/group"), d.entry.name)
                             : std::vector<std::string>{};
  for (const auto& g : d.entry.groups) {
    if (std::ranges::find(have, g) == have.end()) d.missing_groups.push_back(g);
  }
  std::ranges::sort(d.missing_groups);
  if (!d.exists) {
    std::vector<std::string> parts;
    if (d.entry.shell) parts.push_back("shell " + *d.entry.shell);
    if (!d.entry.groups.empty()) parts.push_back("groups " + join(d.entry.groups, ", "));
    s.check = CheckResult::NeedsChange;
    s.summary = parts.empty() ? std::format("{}  create", d.entry.name)
                              : std::format("{}  create ({})", d.entry.name, join(parts, "; "));
    return {};
  }
  std::vector<std::string> parts;
  if (d.shell_differs) parts.push_back("set shell " + *d.entry.shell);
  if (!d.missing_groups.empty()) parts.push_back("add groups " + join(d.missing_groups, ", "));
  s.check = parts.empty() ? CheckResult::Satisfied : CheckResult::NeedsChange;
  s.is_update = true;
  s.summary = std::format("{}  {}", d.entry.name, join(parts, "; "));
  return {};
}

// ---- locale ------------------------------------------------------------------

Status check_locale(Step& s, CheckContext& ctx) {
  auto& d = std::get<LocaleDesired>(s.desired);
  if (d.part == "locales") {
    const auto text = ctx.fs.read(ctx.paths.physical("/etc/locale.gen"));
    const LocaleGen gen = parse_locale_gen(text.value_or(""));
    std::vector<std::string> missing;
    for (const auto& l : d.locales) {
      if (!gen.active.contains(l)) missing.push_back(l);
    }
    s.check = missing.empty() ? CheckResult::Satisfied : CheckResult::NeedsChange;
    s.is_update = text.has_value();
    s.summary = std::format("enable {}  (/etc/locale.gen)", join(missing, ", "));
    return {};
  }
  const auto text = ctx.fs.read(ctx.paths.physical("/etc/locale.conf"));
  const auto lang = text ? locale_conf_lang(*text) : std::nullopt;
  s.check = lang == d.default_locale ? CheckResult::Satisfied : CheckResult::NeedsChange;
  s.is_update = lang.has_value();
  s.summary = std::format("LANG={}  (/etc/locale.conf)", d.default_locale);
  return {};
}

// ---- actions -----------------------------------------------------------------

Status check_action(Step& s, CheckContext& ctx) {
  const auto& d = std::get<ActionDesired>(s.desired);
  s.summary = std::format("{}  [imperative]", d.name);
  const auto& states = other_pass(s, ctx) ? ctx.other_states : ctx.states;
  auto st = states.find(s.module);
  auto fp = ctx.fingerprints.find(s.module);
  const bool unchanged = st != states.end() && fp != ctx.fingerprints.end() &&
                         st->second.status == "applied" && st->second.fingerprint == fp->second;
  if (unchanged && !ctx.refresh.contains(s.module)) {
    s.check = other_pass(s, ctx) ? CheckResult::Satisfied : CheckResult::SkippedFp;
    return {};
  }
  if (other_pass(s, ctx) || ctx.host == nullptr) {
    // Never run another pass's imperative check; it is pending until that pass records it.
    s.check = CheckResult::OtherMode;
    return {};
  }
  PhaseScope phase(*ctx.host, Phase::Check);
  auto r = ctx.host->call("action", {s.module, d.name, "check", ctx.action_info});
  if (!r) return std::unexpected(r.error());
  s.check = r->as_bool() ? CheckResult::Satisfied : CheckResult::NeedsChange;
  return {};
}

}  // namespace

LocaleGen parse_locale_gen(const std::string& text) {
  LocaleGen g;
  int section = 0;  // 0 before, 1 in block, 2 after
  for (const auto& line : split(text, '\n', /*keep_empty=*/true)) {
    const std::string_view t = trim(line);
    if (t == kBlockBegin) {
      g.has_block = true;
      section = 1;
      continue;
    }
    if (t == kBlockEnd && section == 1) {
      section = 2;
      continue;
    }
    if (!t.empty() && !t.starts_with('#')) g.active.insert(std::string(t));
    (section == 0 ? g.before : section == 1 ? g.block : g.after).push_back(line);
  }
  while (!g.before.empty() && g.before.back().empty() && !g.has_block) g.before.pop_back();
  while (!g.after.empty() && g.after.back().empty()) g.after.pop_back();
  return g;
}

std::string render_locale_gen(const LocaleGen& gen, const std::vector<std::string>& block) {
  std::string out;
  for (const auto& l : gen.before) out += l + "\n";
  out += std::string(kBlockBegin) + "\n";
  for (const auto& l : block) out += l + "\n";
  out += std::string(kBlockEnd) + "\n";
  for (const auto& l : gen.after) out += l + "\n";
  return out;
}

std::optional<std::string> locale_conf_lang(const std::string& text) {
  for (const auto& line : split(text, '\n')) {
    const std::string_view t = trim(line);
    if (t.starts_with("LANG=")) {
      std::string v(t.substr(5));
      if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
      return v;
    }
  }
  return std::nullopt;
}

Status check_step(Step& s, CheckContext& ctx) {
  s.check = CheckResult::Pending;
  s.summary.clear();
  s.blocked_reason.clear();
  s.adopting = s.record_only = s.is_update = false;
  Status r;
  switch (s.cls) {
    case StepClass::Env:
      r = std::holds_alternative<BlockDesired>(s.desired) ? check_block(s, ctx) : check_file(s, ctx);
      break;
    case StepClass::Files:
    case StepClass::Udev: r = check_file(s, ctx); break;
    case StepClass::Upgrade: r = check_upgrade(s, ctx); break;
    case StepClass::Packages: r = check_packages(s, ctx); break;
    case StepClass::Services: r = check_service(s, ctx); break;
    case StepClass::Users: r = check_user(s, ctx); break;
    case StepClass::Locale: r = check_locale(s, ctx); break;
    case StepClass::Action: r = check_action(s, ctx); break;
    case StepClass::Post: return {};  // resolved in check_all
  }
  if (!r) return r;
  finish_other(s, ctx);
  return {};
}

Status check_all(std::vector<Step>& steps, CheckContext& ctx) {
  // Batch one installed() query per backend for every package in scope.
  if (ctx.backends != nullptr) {
    std::map<std::string, std::vector<std::string>> by_backend;
    for (const auto& s : steps) {
      if (s.cls != StepClass::Packages) continue;
      const auto& d = std::get<PackagesDesired>(s.desired);
      by_backend[d.backend].insert(by_backend[d.backend].end(), d.names.begin(), d.names.end());
    }
    for (auto& [name, all] : by_backend) {
      Backend* b = ctx.backends->find("packages", name);
      if (b == nullptr) continue;  // reported by check_packages
      std::ranges::sort(all);
      all.erase(std::ranges::unique(all).begin(), all.end());
      auto r = b->installed(all);
      if (!r) return std::unexpected(r.error());
      ctx.installed_cache[name] = *r;
    }
  }
  for (auto& s : steps) {
    if (auto r = check_step(s, ctx); !r) return r;
  }
  for (auto& s : steps) {
    if (s.cls != StepClass::Post) continue;
    const auto& p = std::get<PostDesired>(s.desired);
    bool now = false, later = false;
    for (const auto& c : steps) {
      if (c.cls != p.after) continue;
      if (c.check == CheckResult::NeedsChange || (ctx.refresh.contains(c.module) && !other_pass(c, ctx))) now = true;
      if (c.check == CheckResult::OtherMode) later = true;
    }
    s.summary = p.what == "udev-reload" ? "udevadm control --reload" : "locale-gen";
    s.check = (now && !other_pass(s, ctx)) ? CheckResult::NeedsChange
              : (later || (now && other_pass(s, ctx))) ? CheckResult::OtherMode
                                                        : CheckResult::Satisfied;
  }
  return {};
}

}  // namespace repro
