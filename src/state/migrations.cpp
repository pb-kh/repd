#include "state/migrations.hpp"

#include <array>

namespace repd {
namespace {

constexpr std::string_view kV1 = R"sql(
CREATE TABLE meta(
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE runs(
  id          INTEGER PRIMARY KEY,
  started_at  TEXT NOT NULL,
  finished_at TEXT,
  mode        TEXT NOT NULL CHECK (mode IN ('user','system')),
  profile     TEXT,
  argv        TEXT NOT NULL,
  repro_version TEXT NOT NULL,
  outcome     TEXT CHECK (outcome IN ('ok','failed','interrupted','plan-error')),
  exit_code   INTEGER,
  n_applied INTEGER DEFAULT 0, n_skipped INTEGER DEFAULT 0,
  n_failed  INTEGER DEFAULT 0, n_pending_other INTEGER DEFAULT 0
);

CREATE TABLE run_providers(
  run_id    INTEGER NOT NULL REFERENCES runs(id),
  interface TEXT NOT NULL,
  provider  TEXT NOT NULL,
  source    TEXT NOT NULL,
  PRIMARY KEY (run_id, interface)
);

CREATE TABLE run_modules(
  run_id      INTEGER NOT NULL REFERENCES runs(id),
  module      TEXT NOT NULL,
  fingerprint TEXT NOT NULL,
  status      TEXT NOT NULL CHECK (status IN ('ok','failed','interrupted','skipped','blocked')),
  PRIMARY KEY (run_id, module)
);

CREATE TABLE steps(
  run_id      INTEGER NOT NULL REFERENCES runs(id),
  seq         INTEGER NOT NULL,
  module      TEXT NOT NULL,
  step_id     TEXT NOT NULL,
  class       TEXT NOT NULL,
  imperative  INTEGER NOT NULL DEFAULT 0,
  status      TEXT NOT NULL CHECK (status IN ('ok','skip','fail','interrupted','blocked')),
  started_at  TEXT NOT NULL,
  finished_at TEXT,
  error       TEXT,
  detail      TEXT,
  adopted     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (run_id, seq)
);
CREATE INDEX steps_by_module ON steps(module, run_id);

CREATE TABLE module_state(
  module      TEXT PRIMARY KEY,
  fingerprint TEXT NOT NULL,
  status      TEXT NOT NULL,
  last_run_id INTEGER NOT NULL REFERENCES runs(id),
  updated_at  TEXT NOT NULL
);

CREATE TABLE managed_files(
  path        TEXT PRIMARY KEY,
  kind        TEXT NOT NULL CHECK (kind IN ('link','copy','create','env','udev','locale-block')),
  digest      TEXT,
  link_target TEXT,
  module      TEXT NOT NULL,
  updated_at  TEXT NOT NULL
);

CREATE TABLE managed_packages(
  backend TEXT NOT NULL, name TEXT NOT NULL, module TEXT NOT NULL,
  first_run_id INTEGER NOT NULL REFERENCES runs(id),
  PRIMARY KEY (backend, name)
);
)sql";

// The project was renamed from repro to repd.
constexpr std::string_view kV2 = R"sql(
ALTER TABLE runs RENAME COLUMN repro_version TO repd_version;
)sql";

constexpr std::array<std::string_view, 2> kMigrations = {kV1, kV2};

// A TEMP view shadows main.runs for unqualified names.
constexpr std::string_view kV1ReadOnly = R"sql(
CREATE TEMP VIEW runs AS
  SELECT id, started_at, finished_at, mode, profile, argv, repro_version AS repd_version, outcome,
         exit_code, n_applied, n_skipped, n_failed, n_pending_other
  FROM main.runs;
)sql";

}  // namespace

std::span<const std::string_view> migrations() { return kMigrations; }

std::string_view read_only_compat(int version) { return version == 1 ? kV1ReadOnly : ""; }

}  // namespace repd
