#include "cli/commands.hpp"

#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <print>

#include "apply/engine.hpp"
#include "apply/signals.hpp"
#include "cli/output.hpp"
#include "cli/preview.hpp"
#include "cli/prompt.hpp"
#include "plan/builder.hpp"
#include "plan/graph.hpp"
#include "plugins/loader.hpp"
#include "state/run_lock.hpp"
#include "util/strings.hpp"

namespace repd {

int report(const Error& e) {
  print_error(e);
  return e.exit_code();
}

std::string version_string() {
  return std::format("repd {} (lua-api {}, plugin-abi {}, luajit {}, sqlite {})", REPD_VERSION,
                     REPD_LUA_API_VERSION, REPD_PLUGIN_ABI_VERSION, REPD_LUAJIT_COMMIT,
                     sqlite3_libversion());
}

namespace {

std::string pad(std::string_view s, std::size_t width) {
  std::string out(s);
  // Column widths count code points so "⚠" aligns like one character.
  std::size_t cps = 0;
  for (unsigned char c : s) cps += (c & 0xC0) != 0x80 ? 1 : 0;
  if (cps < width) out.append(width - cps, ' ');
  return out;
}

std::string table(const std::vector<std::vector<std::string>>& rows) {
  std::vector<std::size_t> widths;
  for (const auto& r : rows) {
    for (std::size_t i = 0; i < r.size(); ++i) {
      std::size_t cps = 0;
      for (unsigned char c : r[i]) cps += (c & 0xC0) != 0x80 ? 1 : 0;
      if (widths.size() <= i) widths.push_back(0);
      widths[i] = std::max(widths[i], cps);
    }
  }
  std::string out;
  for (const auto& r : rows) {
    std::string line;
    for (std::size_t i = 0; i < r.size(); ++i) {
      line += i + 1 == r.size() ? r[i] : pad(r[i], widths[i]) + "  ";
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    out += line + "\n";
  }
  return out;
}

Result<std::unique_ptr<Session>> loaded_session(const GlobalOptions& g, Mode mode) {
  auto s = Session::open(g, mode);
  if (!s) return std::unexpected(s.error());
  if (auto r = (*s)->load(); !r) return std::unexpected(r.error());
  return s;
}

Result<Db> open_store(const Session& s, Mode mode, Db::Access access) {
  auto db = Db::open(s.db_path(mode), access);
  if (!db && mode == Mode::System && !s.paths.is_root()) {
    return fail(
        ec::kPrivilege, "the system state store is only readable by root",
        "run: sudo repd " + std::string(access == Db::Access::ReadOnly ? "status -s" : "sync -s"));
  }
  return db;
}

}  // namespace

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

int cmd_init(const GlobalOptions& g, const std::optional<std::string>& dir, bool force) {
  GlobalOptions opts = g;
  if (dir) opts.config = dir;
  auto paths = Paths::detect(Env::from_process(), PathsOptions{opts.config});
  if (!paths) return report(paths.error());
  RealFs fs;
  const ConfigRepository repo = ConfigRepository::at(paths->config_root());
  if (fs.lstat(repo.entry).exists() && !force) {
    return report(Error{std::string(ec::kConfig),
                        std::format("{} already exists", paths->display(repo.entry)),
                        "use --force to rewrite the starter files (other files are never deleted)"});
  }
  const std::vector<std::pair<std::string, std::string>> files = {
      {repo.entry,
       "-- repd configuration entry point (lua-api 1). See docs/lua-api.md.\n"
       "rp.profile {\n"
       "  name = \"main\",\n"
       "  description = \"Default setup\",\n"
       "  default = true,\n"
       "  use = { \"base\" },\n"
       "}\n"},
      {repo.modules_dir + "/base/init.lua",
       "-- A starter module: one environment variable and one managed file.\n"
       "return rp.module {\n"
       "  description = \"Base environment\",\n"
       "  env = { EDITOR = \"vi\" },\n"
       "  files = {\n"
       "    { dest = \"~/.config/example.conf\", src = \"example.conf\", action = \"copy\" },\n"
       "  },\n"
       "}\n"},
      {repo.modules_dir + "/base/files/example.conf", "# Example file managed by repd.\n"},
      {repo.root + "/.gitignore",
       "# repd keeps its state outside the repository.\n.*.repd-tmp-*\n"},
  };
  for (const auto& [path, content] : files) {
    if (auto r = fs.mkdirs(dirname(path), 0755); !r) return report(r.error());
    if (auto r = fs.write_atomic(path, content, 0644); !r) return report(r.error());
  }
  if (auto r = fs.mkdirs(repo.lua_plugins_dir, 0755); !r) return report(r.error());
  std::println("initialised {}", paths->display(repo.root));
  std::println("next: repd sync -u --dry-run");
  return kExitOk;
}

// ---------------------------------------------------------------------------
// sync
// ---------------------------------------------------------------------------

int cmd_sync(const GlobalOptions& g, const SyncArgs& a, const std::vector<std::string>& argv) {
  if (a.user == a.system) {
    return report(Error{std::string(ec::kUsage),
                        "sync needs exactly one of -u (user pass) or -s (system pass)",
                        "run: repd sync -u   (then: sudo repd sync -s)"});
  }
  const Mode mode = a.system ? Mode::System : Mode::User;
  auto session = Session::open(g, mode);
  if (!session) return report(session.error());
  Session& s = **session;
  if (mode == Mode::System && !s.paths.is_root()) {
    return report(
        Error{std::string(ec::kPrivilege), "repd sync -s needs root", "run: sudo repd sync -s"});
  }
  if (mode == Mode::User && ::geteuid() == 0 && s.paths.user().via_sudo) {
    return report(Error{std::string(ec::kPrivilege), "the user pass must not run as root",
                        "run it without sudo: repd sync -u"});
  }
  auto providers = parse_provider_flags(a.providers);
  if (!providers) return report(providers.error());
  install_signal_handlers();

  std::optional<RunLock> run_lock;
  if (!a.dry_run) {
    auto l = RunLock::acquire(s.paths.state_dir(mode), mode);
    if (!l) return report(l.error());
    run_lock.emplace(std::move(*l));
  }
  if (auto r = s.load(); !r) return report(r.error());

  auto db = open_store(s, mode, a.dry_run ? Db::Access::ReadOnly : Db::Access::ReadWrite);
  if (!db) return report(db.error());
  std::optional<Ledger> ledger;
  if (db->is_open()) ledger.emplace(*db);

  PlanRequest req;
  req.modules = a.modules;
  req.profile = a.profile;
  req.providers = *providers;
  req.reselect = {a.reselect.begin(), a.reselect.end()};
  req.refresh = {a.refresh.begin(), a.refresh.end()};
  req.adopt = a.adopt;
  req.upgrade = a.upgrade;
  req.interactive = can_prompt(a.yes);
  auto planned = make_plan(s, req, ledger ? &*ledger : nullptr);
  if (!planned) {
    // Journal the failed attempt (outcome plan-error); nothing was changed.
    if (!a.dry_run && ledger) {
      if (auto run =
              ledger->insert_run(to_string(mode), a.profile, join(argv, " "), REPD_VERSION)) {
        (void)ledger->finish_run(*run, "plan-error", planned.error().exit_code(), RunCounts{});
      }
    }
    return report(planned.error());
  }
  Plan& plan = planned->plan;
  const Style style = Style::detect(1, g.no_color);
  std::print("{}", render_plan(plan, style));
  std::fflush(stdout);
  if (a.dry_run) return kExitOk;

  // The plan succeeded: record provider decisions (only if they changed).
  LockFile lock = planned->lock;
  if (merge_decisions(lock, plan.providers, utc_now())) {
    if (auto r = write_lock(*s.host, s.fs, s.repo, lock); !r) return report(r.error());
  }

  List argv_list;
  for (const auto& x : argv) argv_list.emplace_back(x);
  auto argv_text = s.host->call("encode", {Value{argv_list}});
  ApplyContext actx(s.paths, s.fs);
  actx.mode = mode;
  actx.backends = &s.backends;
  actx.host = s.host.get();
  CheckContext cctx = make_check_context(s, plan, req, &*ledger);
  actx.action_info = cctx.action_info;
  std::string plugin_log;
  set_plugin_log_sink(&plugin_log);
  auto result = apply_plan(plan, actx, cctx, *ledger, argv_text ? argv_text->as_string() : "{}",
                           [](std::string_view line) {
                             std::println("{}", line);
                             std::fflush(stdout);
                           });
  set_plugin_log_sink(nullptr);
  std::print("{}", render_apply_summary(plan, result));
  std::fflush(stdout);
  if (result.error) {
    Error e = *result.error;
    if (e.code == ec::kStep && e.hint.empty()) e.hint = "fix the cause, then run the same command again";
    print_error(e);
  }
  return result.exit_code;
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------

int cmd_status(const GlobalOptions& g, Mode mode, const std::optional<std::string>& profile) {
  auto session = loaded_session(g, mode);
  if (!session) return report(session.error());
  Session& s = **session;
  auto db = open_store(s, mode, Db::Access::ReadOnly);
  if (!db) return report(db.error());
  std::optional<Ledger> ledger;
  if (db->is_open()) ledger.emplace(*db);
  PlanRequest req;
  req.profile = profile;
  auto planned = make_plan(s, req, ledger ? &*ledger : nullptr);
  if (!planned) return report(planned.error());
  const Plan& plan = planned->plan;
  const auto states = ledger ? ledger->module_states() : std::map<std::string, ModuleState>{};
  std::vector<std::vector<std::string>> rows = {{"MODULE", "STATUS", "LAST-RUN"}};
  for (const auto& m : plan.selected) {
    bool unsatisfied = false;
    // steps_for: a module's packages may be installed by another module's batch.
    for (const auto* st : plan.steps_for(m)) {
      if (st->check == CheckResult::NeedsChange || st->check == CheckResult::Blocked) unsatisfied = true;
    }
    auto it = states.find(m);
    std::string status = "applied";
    if (it == states.end()) status = "pending";
    else if (it->second.status == "failed") status = "failed";
    else if (it->second.fingerprint != plan.fingerprints.at(m)) status = "stale";
    else if (unsatisfied || it->second.status != "applied") status = "pending";
    std::string last = "-";
    if (it != states.end()) {
      last = std::format("#{} {}", it->second.last_run_id, it->second.updated_at);
    }
    rows.push_back({m, status, last});
  }
  std::print("{}", table(rows));
  // One line for the other pass.
  const Mode o = other(mode);
  if (!planned->other_store_readable) {
    std::println("{}: unavailable (run: {})", to_string(o),
                 o == Mode::System ? "sudo repd status -s" : "repd status -u");
  } else {
    int pending_modules = 0;
    std::set<std::string> seen;
    for (const auto& st : plan.steps) {
      if (st.check == CheckResult::OtherMode && seen.insert(st.module).second) ++pending_modules;
    }
    std::println("{}: {} pending ({} module{}){}", to_string(o), plan.pending_other(), pending_modules,
                 pending_modules == 1 ? "" : "s",
                 plan.pending_other() > 0 ? std::format(" — run: {}", other_pass_hint(mode)) : "");
  }
  return kExitOk;
}

// ---------------------------------------------------------------------------
// journal
// ---------------------------------------------------------------------------

namespace {

long long seconds_between(const std::string& a, const std::optional<std::string>& b) {
  if (!b) return 0;
  auto parse = [](const std::string& t) {
    std::tm tm{};
    if (::strptime(t.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm) == nullptr) return 0LL;
    return static_cast<long long>(::timegm(&tm));
  };
  return std::max(0LL, parse(*b) - parse(a));
}

void print_run(const Ledger& ledger, const RunRow& r, const Paths& paths) {
  std::println("run #{}  {}  profile {}  {} → {}  {}  (applied {}, skipped {}, failed {}, pending {})", r.id,
               r.mode, r.profile.value_or("-"), r.started_at, r.finished_at.value_or("…"),
               r.outcome.value_or("running"), r.counts.applied, r.counts.skipped, r.counts.failed,
               r.counts.pending_other);
  for (const auto& st : ledger.steps(r.id)) {
    std::string line = std::format("  {:>3}  {:<4}  {}  {}s", st.seq,
                                   st.status == "fail" ? "FAIL" : st.status, st.step_id,
                                   seconds_between(st.started_at, st.finished_at));
    if (st.imperative) line += "  [imperative]";
    if (st.adopted) {
      std::string backup = "?";
      if (st.detail) {
        const auto pos = st.detail->find("backup=\"");
        if (pos != std::string::npos) {
          const auto end = st.detail->find('"', pos + 8);
          backup = st.detail->substr(pos + 8, end - pos - 8);
        }
      }
      line += std::format("  (adopted, backup: {})", paths.display(backup));
    }
    if (st.error) line += "  " + *st.error;
    std::println("{}", line);
  }
}

}  // namespace

int cmd_journal(const GlobalOptions& g, Mode mode, int last, std::optional<std::int64_t> run) {
  auto session = Session::open(g, mode);
  if (!session) return report(session.error());
  auto db = open_store(**session, mode, Db::Access::ReadOnly);
  if (!db) return report(db.error());
  if (!db->is_open()) {
    std::println("no runs recorded yet in {} mode", to_string(mode));
    return kExitOk;
  }
  Ledger ledger(*db);
  if (run) {
    auto r = ledger.run(*run);
    if (!r) {
      return report(Error{std::string(ec::kUnknown),
                          std::format("no run #{} in {} mode", *run, to_string(mode)),
                          "list recent runs with `repd journal`"});
    }
    print_run(ledger, *r, (*session)->paths);
    return kExitOk;
  }
  const auto runs = ledger.recent_runs(last);
  if (runs.empty()) std::println("no runs recorded yet in {} mode", to_string(mode));
  for (std::size_t i = 0; i < runs.size(); ++i) {
    if (i > 0) std::println("");
    print_run(ledger, runs[i], (*session)->paths);
  }
  return kExitOk;
}

// ---------------------------------------------------------------------------
// list / show / plugins
// ---------------------------------------------------------------------------

namespace {

std::string plugins_table(const BackendRegistry& reg, const Paths& paths) {
  std::vector<std::vector<std::string>> rows = {{"NAMESPACE", "NAME", "VERSION", "ABI", "ORIGIN", "PATH"}};
  for (const auto* b : reg.all()) {
    const auto& i = b->info();
    const bool repo = i.origin == "repo";
    rows.push_back({i.ns, i.name, i.version, i.lua ? "-" : std::to_string(i.abi), repo ? "repo ⚠" : i.origin,
                    repo ? i.path + " (reduced trust)" : paths.display(i.path)});
  }
  std::string out = table(rows);
  if (!reg.refused().empty()) {
    out += "refused:\n";
    for (const auto& r : reg.refused()) out += std::format("  {}  {}\n", r.path, r.reason);
  }
  if (!reg.duplicates().empty()) {
    out += "duplicates (ignored):\n";
    for (const auto& d : reg.duplicates()) out += "  " + d + "\n";
  }
  return out;
}

}  // namespace

int cmd_plugins_list(const GlobalOptions& g) {
  auto session = Session::open(g, Mode::User);
  if (!session) return report(session.error());
  Session& s = **session;
  if (auto r = s.load(); !r) {
    // Without a usable repository, still report native plugins.
    if (r.error().code != ec::kConfig) return report(r.error());
    BackendRegistry native;
    load_native_plugins(s.fs, s.paths, Mode::User, native);
    std::print("{}", plugins_table(native, s.paths));
    return kExitOk;
  }
  std::print("{}", plugins_table(s.backends, s.paths));
  return kExitOk;
}

int cmd_list(const GlobalOptions& g, const std::string& what, const std::optional<std::string>& profile) {
  if (what == "plugins") return cmd_plugins_list(g);
  const std::vector<std::string> kinds = {"modules", "interfaces", "profiles", "providers", "plugins"};
  if (std::ranges::find(kinds, what) == kinds.end()) {
    const std::string c = closest_match(what, kinds);
    return report(Error{std::string(ec::kUsage), std::format("cannot list '{}'", what),
                        c.empty() ? "choose one of: " + join(kinds, ", ") : std::format("did you mean '{}'?", c)});
  }
  auto session = loaded_session(g, Mode::User);
  if (!session) return report(session.error());
  Session& s = **session;
  std::vector<std::vector<std::string>> rows;
  if (what == "modules") {
    std::map<std::string, std::vector<std::string>> offered;  // optional module -> who offers it
    for (const auto& [name, m] : s.modules) {
      for (const auto& o : m.optional_) offered[o].push_back(name);
    }
    for (const auto& [name, m] : s.modules) {
      if (m.is_interface()) continue;
      std::vector<std::string> notes;
      if (m.provides) notes.push_back("provides " + *m.provides);
      if (!m.integrates.empty()) notes.push_back("integrates " + join(m.integrates, " + "));
      if (auto it = offered.find(name); it != offered.end()) notes.push_back("optional for " + join(it->second, ", "));
      if (!m.auto_sync) notes.push_back("auto_sync = false");
      rows.push_back({name, join(notes, "; "), m.description});
    }
  } else if (what == "interfaces") {
    Resolver r(s.modules, ResolverInputs{});
    for (const auto& [name, m] : s.modules) {
      if (!m.is_interface()) continue;
      std::vector<std::string> names;
      for (const auto* c : r.candidates(name)) names.push_back(c->name);
      rows.push_back({name, m.description, "providers: " + (names.empty() ? "(none)" : join(names, ", "))});
    }
  } else if (what == "profiles") {
    for (const auto& p : s.root.profiles) {
      std::vector<std::string> use;
      for (const auto& u : p.use) {
        if (u.auto_sync == false) use.push_back(u.name + " (auto_sync = false)");
        else use.push_back(u.extras ? u.name + "+" : u.name);
      }
      rows.push_back({p.name, p.is_default ? "(default)" : "", p.description, "use: " + join(use, ", ")});
    }
    std::ranges::sort(rows);
  } else {  // providers
    PlanRequest req;
    req.profile = profile;
    const Profile* prof = profile ? s.root.find_profile(*profile) : s.root.default_profile();
    if (profile && prof == nullptr) {
      return report(Error{std::string(ec::kUnknown), std::format("unknown profile '{}'", *profile),
                          "list profiles with `repd list profiles`"});
    }
    auto lock = read_lock(*s.host, s.fs, s.repo);
    if (!lock) return report(lock.error());
    ResolverInputs in;
    if (prof != nullptr) in.profile = prof->providers;
    in.pins = s.root.pins;
    in.lock = lock->providers;
    Resolver resolver(s.modules, in);
    std::string out;
    for (const auto& [name, m] : s.modules) {
      if (!m.is_interface()) continue;
      auto chosen = resolver.resolve(name, {});
      out += std::format("{}  {}\n", name, m.description);
      std::vector<std::vector<std::string>> prow;
      for (const auto* c : resolver.candidates(name)) {
        const bool mark = chosen && chosen->provider == c->name;
        prow.push_back({mark ? "  *" : "   ", c->name, std::format("priority {}", c->priority),
                        mark ? "(" + chosen->source + ")" : ""});
      }
      if (prow.empty()) out += "     (no providers)\n";
      out += table(prow);
    }
    std::print("{}", out);
    return kExitOk;
  }
  std::print("{}", table(rows));
  return kExitOk;
}

int cmd_show(const GlobalOptions& g, const std::string& module, const std::optional<std::string>& profile) {
  auto session = loaded_session(g, Mode::User);
  if (!session) return report(session.error());
  Session& s = **session;
  auto it = s.modules.find(module);
  if (it == s.modules.end()) {
    std::vector<std::string> names;
    for (const auto& [n, _] : s.modules) names.push_back(n);
    const std::string c = closest_match(module, names);
    return report(Error{std::string(ec::kUnknown), std::format("unknown module '{}'", module),
                        c.empty() ? "list modules with `repd list modules`"
                                  : std::format("did you mean '{}'?", c)});
  }
  if (it->second.is_interface()) {
    Resolver r(s.modules, ResolverInputs{});
    std::println("interface {}\n  {}", module, it->second.description);
    for (const auto* c : r.candidates(module)) std::println("  provider {} (priority {})", c->name, c->priority);
    return kExitOk;
  }
  const Profile* prof = profile ? s.root.find_profile(*profile) : s.root.default_profile();
  if (profile && prof == nullptr) {
    return report(Error{std::string(ec::kUnknown), std::format("unknown profile '{}'", *profile),
                        "list profiles with `repd list profiles`"});
  }
  Value overrides{Map{}};
  if (prof != nullptr) {
    for (const auto& u : prof->use) {
      if (u.name == module && u.params.is_map()) overrides = u.params;
    }
  }
  auto resolved = s.eval->resolve(module, overrides);
  if (!resolved) return report(resolved.error());
  auto pretty = s.host->call("pretty", {resolved->decl});
  if (!pretty) return report(pretty.error());
  std::println("module {}", module);
  std::println("{}", pretty->as_string());
  // Providers for its requires (never prompts).
  auto lock = read_lock(*s.host, s.fs, s.repo);
  ResolverInputs in;
  if (prof != nullptr) in.profile = prof->providers;
  in.pins = s.root.pins;
  if (lock) in.lock = lock->providers;
  Resolver resolver(s.modules, in);
  bool header = false;
  for (const auto& r : it->second.requires_) {
    if (!s.modules.at(r).is_interface()) continue;
    if (!header) std::println("providers:");
    header = true;
    auto p = resolver.resolve(r, {module});
    if (p) {
      std::println("  {} = {} ({})", r, p->provider, p->source);
    } else {
      std::vector<std::string> names;
      for (const auto* c : resolver.candidates(r)) names.push_back(c->name);
      std::println("  {} = <unresolved: candidates {}>", r, join(names, ", "));
    }
  }
  // Each packages backend the module names; one that is not installed is shown
  // as a system-pass step, which is where most package managers run.
  const std::string pkg_default = default_package_backend(s.option("packages.default_backend"));
  std::map<std::string, Mode> pkg_backends;
  for (const auto& entry : resolved->packages) {
    const std::string name = split_package(entry, pkg_default).backend;
    if (name.empty()) continue;
    const Backend* b = s.backends.find("packages", name);
    pkg_backends[name] = b != nullptr ? b->info().privilege : Mode::System;
  }
  auto targets = resolve_env_targets(s.option("env.targets"), s.paths, s.fs);
  if (!targets) return report(targets.error());
  BuildContext bctx{s.paths, s.fs, s.repo, std::move(pkg_backends), {}, *targets, {}, pkg_default};
  if (resolved->locale) bctx.all_locales = resolved->locale->locales;
  auto steps = build_module_steps(*resolved, bctx);
  if (!steps) return report(steps.error());
  std::println("steps:");
  for (const auto& st : *steps) std::println("  {}  [{} pass]", st.id, to_string(st.privilege));
  return kExitOk;
}

}  // namespace repd
