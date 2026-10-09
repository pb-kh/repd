#include "state/ledger.hpp"

#include "eval/host.hpp"  // utc_now

namespace repro {

Status Ledger::transaction(const std::function<Status()>& fn) {
  if (auto r = db_.exec("BEGIN IMMEDIATE"); !r) return r;
  auto r = fn();
  if (!r) {
    (void)db_.exec("ROLLBACK");
    return r;
  }
  return db_.exec("COMMIT");
}

Result<std::int64_t> Ledger::insert_run(std::string_view mode,
                                        const std::optional<std::string>& profile,
                                        std::string_view argv, std::string_view version) {
  auto st = db_.prepare(
      "INSERT INTO runs(started_at, mode, profile, argv, repro_version) VALUES (?, ?, ?, ?, ?)");
  if (!st) return std::unexpected(st.error());
  st->bind(1, utc_now()).bind(2, mode).bind(3, profile).bind(4, argv).bind(5, version);
  if (auto r = st->run(); !r) return std::unexpected(r.error());
  return db_.last_insert_id();
}

Status Ledger::finish_run(std::int64_t run_id, std::string_view outcome, int exit_code,
                          const RunCounts& c) {
  auto st = db_.prepare(
      "UPDATE runs SET finished_at = ?, outcome = ?, exit_code = ?, n_applied = ?, "
      "n_skipped = ?, n_failed = ?, n_pending_other = ? WHERE id = ?");
  if (!st) return std::unexpected(st.error());
  st->bind(1, utc_now())
      .bind(2, outcome)
      .bind(3, std::int64_t{exit_code})
      .bind(4, std::int64_t{c.applied})
      .bind(5, std::int64_t{c.skipped})
      .bind(6, std::int64_t{c.failed})
      .bind(7, std::int64_t{c.pending_other})
      .bind(8, run_id);
  return st->run();
}

Status Ledger::set_run_providers(
    std::int64_t run_id, const std::map<std::string, std::pair<std::string, std::string>>& p) {
  for (const auto& [iface, prov] : p) {
    auto st = db_.prepare(
        "INSERT OR REPLACE INTO run_providers(run_id, interface, provider, source) "
        "VALUES (?, ?, ?, ?)");
    if (!st) return std::unexpected(st.error());
    st->bind(1, run_id).bind(2, iface).bind(3, prov.first).bind(4, prov.second);
    if (auto r = st->run(); !r) return r;
  }
  return {};
}

Status Ledger::set_run_module(std::int64_t run_id, std::string_view module,
                              std::string_view fingerprint, std::string_view status) {
  auto st = db_.prepare(
      "INSERT OR REPLACE INTO run_modules(run_id, module, fingerprint, status) VALUES (?, ?, ?, ?)");
  if (!st) return std::unexpected(st.error());
  st->bind(1, run_id).bind(2, module).bind(3, fingerprint).bind(4, status);
  return st->run();
}

Status Ledger::append_step(const StepRow& s) {
  auto st = db_.prepare(
      "INSERT INTO steps(run_id, seq, module, step_id, class, imperative, status, started_at, "
      "finished_at, error, detail, adopted) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  if (!st) return std::unexpected(st.error());
  st->bind(1, s.run_id)
      .bind(2, std::int64_t{s.seq})
      .bind(3, s.module)
      .bind(4, s.step_id)
      .bind(5, s.cls)
      .bind(6, std::int64_t{s.imperative ? 1 : 0})
      .bind(7, s.status)
      .bind(8, s.started_at)
      .bind(9, s.finished_at)
      .bind(10, s.error)
      .bind(11, s.detail)
      .bind(12, std::int64_t{s.adopted ? 1 : 0});
  return st->run();
}

Status Ledger::update_module_state(std::string_view module, std::string_view fingerprint,
                                   std::string_view status, std::int64_t run_id) {
  auto st = db_.prepare(
      "INSERT INTO module_state(module, fingerprint, status, last_run_id, updated_at) "
      "VALUES (?, ?, ?, ?, ?) ON CONFLICT(module) DO UPDATE SET fingerprint = excluded.fingerprint, "
      "status = excluded.status, last_run_id = excluded.last_run_id, updated_at = excluded.updated_at");
  if (!st) return std::unexpected(st.error());
  st->bind(1, module).bind(2, fingerprint).bind(3, status).bind(4, run_id).bind(5, utc_now());
  return st->run();
}

Status Ledger::upsert_managed_file(const ManagedFile& f) {
  auto st = db_.prepare(
      "INSERT INTO managed_files(path, kind, digest, link_target, module, updated_at) "
      "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(path) DO UPDATE SET kind = excluded.kind, "
      "digest = excluded.digest, link_target = excluded.link_target, module = excluded.module, "
      "updated_at = excluded.updated_at");
  if (!st) return std::unexpected(st.error());
  st->bind(1, f.path)
      .bind(2, f.kind)
      .bind(3, f.digest)
      .bind(4, f.link_target)
      .bind(5, f.module)
      .bind(6, utc_now());
  return st->run();
}

Status Ledger::insert_managed_packages(std::string_view backend,
                                       const std::vector<std::string>& names,
                                       std::string_view module, std::int64_t run_id) {
  for (const auto& n : names) {
    auto st = db_.prepare(
        "INSERT OR IGNORE INTO managed_packages(backend, name, module, first_run_id) "
        "VALUES (?, ?, ?, ?)");
    if (!st) return std::unexpected(st.error());
    st->bind(1, backend).bind(2, n).bind(3, module).bind(4, run_id);
    if (auto r = st->run(); !r) return r;
  }
  return {};
}

std::optional<ManagedFile> Ledger::managed_file(const std::string& path) const {
  auto st = db_.prepare(
      "SELECT path, kind, digest, link_target, module FROM managed_files WHERE path = ?");
  if (!st) return std::nullopt;
  st->bind(1, path);
  auto row = st->step();
  if (!row || !*row) return std::nullopt;
  return ManagedFile{st->text(0), st->text(1), st->opt_text(2), st->opt_text(3), st->text(4)};
}

std::map<std::string, ModuleState> Ledger::module_states() const {
  std::map<std::string, ModuleState> out;
  auto st = db_.prepare(
      "SELECT module, fingerprint, status, last_run_id, updated_at FROM module_state");
  if (!st) return out;
  while (true) {
    auto row = st->step();
    if (!row || !*row) break;
    out[st->text(0)] =
        ModuleState{st->text(0), st->text(1), st->text(2), st->integer(3), st->text(4)};
  }
  return out;
}

namespace {

constexpr std::string_view kRunColumns =
    "SELECT id, started_at, finished_at, mode, profile, argv, repro_version, outcome, exit_code, "
    "n_applied, n_skipped, n_failed, n_pending_other FROM runs";

RunRow read_run(const Stmt& st) {
  RunRow r;
  r.id = st.integer(0);
  r.started_at = st.text(1);
  r.finished_at = st.opt_text(2);
  r.mode = st.text(3);
  r.profile = st.opt_text(4);
  r.argv = st.text(5);
  r.repro_version = st.text(6);
  r.outcome = st.opt_text(7);
  if (st.opt_text(8)) r.exit_code = st.integer(8);
  r.counts = RunCounts{static_cast<int>(st.integer(9)), static_cast<int>(st.integer(10)),
                       static_cast<int>(st.integer(11)), static_cast<int>(st.integer(12))};
  return r;
}

}  // namespace

std::vector<RunRow> Ledger::recent_runs(int limit) const {
  std::vector<RunRow> out;
  auto st = db_.prepare(std::string(kRunColumns) + " ORDER BY id DESC LIMIT ?");
  if (!st) return out;
  st->bind(1, std::int64_t{limit});
  while (true) {
    auto row = st->step();
    if (!row || !*row) break;
    out.push_back(read_run(*st));
  }
  return out;
}

std::optional<RunRow> Ledger::run(std::int64_t id) const {
  auto st = db_.prepare(std::string(kRunColumns) + " WHERE id = ?");
  if (!st) return std::nullopt;
  st->bind(1, id);
  auto row = st->step();
  if (!row || !*row) return std::nullopt;
  return read_run(*st);
}

std::map<std::string, std::pair<std::string, std::string>> Ledger::run_providers(
    std::int64_t id) const {
  std::map<std::string, std::pair<std::string, std::string>> out;
  auto st = db_.prepare(
      "SELECT interface, provider, source FROM run_providers WHERE run_id = ? ORDER BY interface");
  if (!st) return out;
  st->bind(1, id);
  while (true) {
    auto row = st->step();
    if (!row || !*row) break;
    out[st->text(0)] = {st->text(1), st->text(2)};
  }
  return out;
}

std::vector<StepRow> Ledger::steps(std::int64_t run_id) const {
  std::vector<StepRow> out;
  auto st = db_.prepare(
      "SELECT run_id, seq, module, step_id, class, imperative, status, started_at, finished_at, "
      "error, detail, adopted FROM steps WHERE run_id = ? ORDER BY seq");
  if (!st) return out;
  st->bind(1, run_id);
  while (true) {
    auto row = st->step();
    if (!row || !*row) break;
    StepRow s;
    s.run_id = st->integer(0);
    s.seq = static_cast<int>(st->integer(1));
    s.module = st->text(2);
    s.step_id = st->text(3);
    s.cls = st->text(4);
    s.imperative = st->integer(5) != 0;
    s.status = st->text(6);
    s.started_at = st->text(7);
    s.finished_at = st->opt_text(8);
    s.error = st->opt_text(9);
    s.detail = st->opt_text(10);
    s.adopted = st->integer(11) != 0;
    out.push_back(std::move(s));
  }
  return out;
}

std::optional<std::int64_t> Ledger::last_run_touching(std::string_view module) const {
  auto st = db_.prepare("SELECT MAX(run_id) FROM run_modules WHERE module = ?");
  if (!st) return std::nullopt;
  st->bind(1, module);
  auto row = st->step();
  if (!row || !*row || !st->opt_text(0)) return std::nullopt;
  return st->integer(0);
}

}  // namespace repro
