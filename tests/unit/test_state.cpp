// SQLite state store, migrations, ledger, and the same-mode run lock (R5, R6).
#include <doctest.h>

#include <sqlite3.h>
#include <unistd.h>

#include <filesystem>

#include "state/db.hpp"
#include "state/ledger.hpp"
#include "state/run_lock.hpp"

using namespace repd;
namespace stdfs = std::filesystem;

namespace {

struct TempDir {
  std::string path;
  TempDir() {
    static int counter = 0;
    path = (stdfs::temp_directory_path() /
            ("repd-state-" + std::to_string(::getpid()) + "-" + std::to_string(counter++)))
               .string();
    stdfs::remove_all(path);
  }
  ~TempDir() { stdfs::remove_all(path); }
};

}  // namespace

TEST_CASE("db: creates the store with 0600 permissions and current schema") {
  TempDir t;
  auto db = Db::open(t.path + "/state/repd.db", Db::Access::ReadWrite);
  REQUIRE(db);
  CHECK(db->schema_version() == Db::kSchemaVersion);
  CHECK((stdfs::status(t.path + "/state/repd.db").permissions() & stdfs::perms::all) ==
        (stdfs::perms::owner_read | stdfs::perms::owner_write));
  CHECK((stdfs::status(t.path + "/state").permissions() & stdfs::perms::all) ==
        stdfs::perms::owner_all);
  // Reopening is idempotent.
  db = Db::open(t.path + "/state/repd.db", Db::Access::ReadWrite);
  REQUIRE(db);
  CHECK(db->schema_version() == Db::kSchemaVersion);
}

TEST_CASE("db: read-only open of a missing store yields no database") {
  TempDir t;
  auto db = Db::open(t.path + "/missing/repd.db", Db::Access::ReadOnly);
  REQUIRE(db);
  CHECK_FALSE(db->is_open());
  CHECK_FALSE(stdfs::exists(t.path + "/missing"));
}

TEST_CASE("db: refuses a store with a newer schema") {
  TempDir t;
  {
    auto db = Db::open(t.path + "/repd.db", Db::Access::ReadWrite);
    REQUIRE(db);
    REQUIRE(db->exec("UPDATE meta SET value = '999' WHERE key = 'schema_version'"));
  }
  auto db = Db::open(t.path + "/repd.db", Db::Access::ReadWrite);
  REQUIRE_FALSE(db);
  CHECK(db.error().code == "E_SCHEMA");
  CHECK(db.error().message.find("999") != std::string::npos);
}

TEST_CASE("db: a schema 1 store (from repro) is readable as is and migrated on write") {
  TempDir t;
  const std::string path = t.path + "/repd.db";
  {
    auto db = Db::open(path, Db::Access::ReadWrite);
    REQUIRE(db);
    REQUIRE(Ledger(*db).insert_run("user", "p", "repro sync -u", "0.1.0"));
    REQUIRE(db->exec("ALTER TABLE runs RENAME COLUMN repd_version TO repro_version;"
                     "UPDATE meta SET value = '1' WHERE key = 'schema_version'"));
  }
  {
    auto db = Db::open(path, Db::Access::ReadOnly);
    REQUIRE(db);
    CHECK(db->schema_version() == 1);
    const auto runs = Ledger(*db).recent_runs(5);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].repd_version == "0.1.0");
  }
  auto db = Db::open(path, Db::Access::ReadWrite);
  REQUIRE(db);
  CHECK(db->schema_version() == 2);
  const auto runs = Ledger(*db).recent_runs(5);
  REQUIRE(runs.size() == 1);
  CHECK(runs[0].repd_version == "0.1.0");
}

TEST_CASE("ledger: runs, steps, module state and managed files") {
  TempDir t;
  auto db = Db::open(t.path + "/repd.db", Db::Access::ReadWrite);
  REQUIRE(db);
  Ledger ledger(*db);
  auto run = ledger.insert_run("user", "laptop", "{\"sync\",\"-u\"}", "0.1.0");
  REQUIRE(run);
  CHECK(*run > 0);

  StepRow s;
  s.run_id = *run;
  s.seq = 1;
  s.module = "base";
  s.step_id = "base/files/~/.bashrc";
  s.cls = "files";
  s.status = "ok";
  s.started_at = "2026-10-09T12:00:00Z";
  s.finished_at = "2026-10-09T12:00:01Z";
  ManagedFile mf{"/home/a/.bashrc", "copy", "abc", std::nullopt, "base"};
  REQUIRE(ledger.transaction([&]() -> Status {
    if (auto r = ledger.append_step(s); !r) return r;
    return ledger.upsert_managed_file(mf);
  }));
  auto got = ledger.managed_file("/home/a/.bashrc");
  REQUIRE(got);
  CHECK(got->digest == "abc");
  CHECK(got->module == "base");
  mf.digest = "def";
  REQUIRE(ledger.upsert_managed_file(mf));
  CHECK(ledger.managed_file("/home/a/.bashrc")->digest == "def");
  CHECK_FALSE(ledger.managed_file("/nope"));

  REQUIRE(ledger.set_run_providers(*run, {{"dev/ide", {"dev/ide/vim", "lock"}}}));
  REQUIRE(ledger.set_run_module(*run, "base", "fp1", "ok"));
  REQUIRE(ledger.update_module_state("base", "fp1", "applied", *run));
  REQUIRE(ledger.insert_managed_packages("fake", {"git", "vim"}, "base", *run));
  REQUIRE(ledger.insert_managed_packages("fake", {"git"}, "other", *run));
  REQUIRE(ledger.finish_run(*run, "ok", 0, RunCounts{1, 2, 0, 3}));

  auto states = ledger.module_states();
  REQUIRE(states.contains("base"));
  CHECK(states["base"].fingerprint == "fp1");
  CHECK(states["base"].status == "applied");

  auto runs = ledger.recent_runs(5);
  REQUIRE(runs.size() == 1);
  CHECK(runs[0].mode == "user");
  CHECK(runs[0].profile == "laptop");
  CHECK(runs[0].outcome == "ok");
  CHECK(runs[0].counts.applied == 1);
  CHECK(runs[0].counts.pending_other == 3);
  CHECK(ledger.run_providers(*run).at("dev/ide").first == "dev/ide/vim");
  auto steps = ledger.steps(*run);
  REQUIRE(steps.size() == 1);
  CHECK(steps[0].step_id == "base/files/~/.bashrc");
  CHECK(ledger.last_run_touching("base") == *run);
}

TEST_CASE("ledger: transaction rolls back on failure") {
  TempDir t;
  auto db = Db::open(t.path + "/repd.db", Db::Access::ReadWrite);
  REQUIRE(db);
  Ledger ledger(*db);
  auto r = ledger.transaction([&]() -> Status {
    if (auto s = ledger.upsert_managed_file({"/x", "create", "d", std::nullopt, "m"}); !s) return s;
    return fail(ec::kStep, "boom", "");
  });
  CHECK_FALSE(r);
  CHECK_FALSE(ledger.managed_file("/x"));
}

TEST_CASE("run lock: exclusive per state dir, reports the holder pid") {
  TempDir t;
  auto a = RunLock::acquire(t.path, Mode::User);
  REQUIRE(a);
  auto b = RunLock::acquire(t.path, Mode::User);
  REQUIRE_FALSE(b);
  CHECK(b.error().code == "E_BUSY");
  CHECK(b.error().message.find("user mode") != std::string::npos);
  CHECK(b.error().message.find(std::to_string(::getpid())) != std::string::npos);
  TempDir other;
  CHECK(RunLock::acquire(other.path, Mode::System));
  a = fail(ec::kBusy, "released", "");  // drops the first lock
  CHECK(RunLock::acquire(t.path, Mode::User));
}
