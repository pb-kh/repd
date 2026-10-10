#include "state/db.hpp"

#include <fcntl.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>

#include "platform/fs.hpp"
#include "state/migrations.hpp"
#include "util/strings.hpp"

namespace repd {

Stmt::~Stmt() {
  if (s_ != nullptr) sqlite3_finalize(s_);
}

Stmt& Stmt::operator=(Stmt&& o) noexcept {
  if (this != &o) {
    if (s_ != nullptr) sqlite3_finalize(s_);
    s_ = o.s_;
    o.s_ = nullptr;
  }
  return *this;
}

Stmt& Stmt::bind(int i, std::string_view v) {
  sqlite3_bind_text(s_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
  return *this;
}

Stmt& Stmt::bind(int i, std::int64_t v) {
  sqlite3_bind_int64(s_, i, v);
  return *this;
}

Stmt& Stmt::bind(int i, std::nullopt_t) {
  sqlite3_bind_null(s_, i);
  return *this;
}

Stmt& Stmt::bind(int i, const std::optional<std::string>& v) {
  return v ? bind(i, std::string_view(*v)) : bind(i, std::nullopt);
}

Result<bool> Stmt::step() {
  const int rc = sqlite3_step(s_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  sqlite3* db = sqlite3_db_handle(s_);
  return fail(ec::kIo, std::format("state database error: {}", sqlite3_errmsg(db)),
              "check permissions of the state directory");
}

Status Stmt::run() {
  for (;;) {
    auto r = step();
    if (!r) return std::unexpected(r.error());
    if (!*r) return {};
  }
}

std::string Stmt::text(int col) const {
  const auto* p = sqlite3_column_text(s_, col);
  return p != nullptr ? reinterpret_cast<const char*>(p) : "";
}

std::optional<std::string> Stmt::opt_text(int col) const {
  if (sqlite3_column_type(s_, col) == SQLITE_NULL) return std::nullopt;
  return text(col);
}

std::int64_t Stmt::integer(int col) const { return sqlite3_column_int64(s_, col); }

Db::~Db() {
  if (db_ != nullptr) sqlite3_close(db_);
}

Db& Db::operator=(Db&& o) noexcept {
  if (this != &o) {
    if (db_ != nullptr) sqlite3_close(db_);
    db_ = o.db_;
    path_ = std::move(o.path_);
    o.db_ = nullptr;
  }
  return *this;
}

Error Db::error(std::string_view what) const {
  return Error{std::string(ec::kIo),
               std::format("{} {}: {}", what, path_, db_ != nullptr ? sqlite3_errmsg(db_) : "?"),
               "check permissions of the state directory"};
}

Result<Db> Db::open(const std::string& path, Access access) {
  Db db;
  db.path_ = path;
  RealFs fs;
  if (access == Access::ReadOnly) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) {
      if (errno == ENOENT) return db;  // no store yet
      return fail(ec::kIo, std::format("cannot read state database {}: {}", path, std::strerror(errno)),
                  "check permissions of the state directory");
    }
    if (sqlite3_open_v2(path.c_str(), &db.db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
      // An unreadable store (e.g. root-owned system DB) is reported by the caller.
      Error e = db.error("cannot open state database");
      return std::unexpected(e);
    }
    sqlite3_busy_timeout(db.db_, 2000);
    const int v = db.schema_version();
    if (v > kSchemaVersion) {
      return fail(ec::kSchema,
                  std::format("state database {} has schema {}, this repd supports {}", path, v,
                              kSchemaVersion),
                  "upgrade repd; downgrading the state store is not supported");
    }
    if (const auto compat = read_only_compat(v); !compat.empty()) {
      if (auto r = db.exec(compat); !r) return std::unexpected(r.error());
    }
    return db;
  }
  if (auto r = fs.mkdirs(dirname(path), 0700); !r) return std::unexpected(r.error());
  // Pre-create the file with 0600 so SQLite never creates it with the umask.
  if (!fs.stat(path).exists()) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd >= 0) ::close(fd);
  }
  if (sqlite3_open_v2(path.c_str(), &db.db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    return std::unexpected(db.error("cannot open state database"));
  }
  sqlite3_busy_timeout(db.db_, 5000);
  if (auto r = db.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;");
      !r) {
    return std::unexpected(r.error());
  }
  if (auto r = db.migrate(); !r) return std::unexpected(r.error());
  return db;
}

Status Db::exec(std::string_view sql) {
  char* msg = nullptr;
  const std::string s(sql);
  if (sqlite3_exec(db_, s.c_str(), nullptr, nullptr, &msg) != SQLITE_OK) {
    Error e{std::string(ec::kIo),
            std::format("state database error in {}: {}", path_, msg != nullptr ? msg : "?"),
            "check permissions of the state directory"};
    sqlite3_free(msg);
    return std::unexpected(e);
  }
  return {};
}

Result<Stmt> Db::prepare(std::string_view sql) {
  sqlite3_stmt* s = nullptr;
  if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &s, nullptr) != SQLITE_OK) {
    return std::unexpected(error("cannot prepare statement for"));
  }
  return Stmt(s);
}

std::int64_t Db::last_insert_id() const { return sqlite3_last_insert_rowid(db_); }

int Db::schema_version() {
  auto st = prepare("SELECT value FROM meta WHERE key = 'schema_version'");
  if (!st) return 0;
  auto row = st->step();
  if (!row || !*row) return 0;
  return std::atoi(st->text(0).c_str());
}

Status Db::migrate() {
  const auto all = migrations();
  // Probe for the meta table before reading the version.
  auto probe = prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name='meta'");
  if (!probe) return std::unexpected(probe.error());
  auto has_meta = probe->step();
  if (!has_meta) return std::unexpected(has_meta.error());
  const int current = *has_meta ? schema_version() : 0;
  if (current > kSchemaVersion) {
    return fail(ec::kSchema,
                std::format("state database {} has schema {}, this repd supports {}", path_,
                            current, kSchemaVersion),
                "upgrade repd; downgrading the state store is not supported");
  }
  if (current == kSchemaVersion) return {};
  if (auto r = exec("BEGIN IMMEDIATE"); !r) return r;
  for (int v = current; v < kSchemaVersion; ++v) {
    if (auto r = exec(all[static_cast<std::size_t>(v)]); !r) {
      (void)exec("ROLLBACK");
      return r;
    }
  }
  const std::string meta = std::format(
      "INSERT INTO meta(key, value) VALUES ('schema_version', '{0}') "
      "ON CONFLICT(key) DO UPDATE SET value = '{0}';"
      "INSERT OR IGNORE INTO meta(key, value) VALUES ('created_by', 'repd {1}');",
      kSchemaVersion, REPD_VERSION);
  if (auto r = exec(meta); !r) {
    (void)exec("ROLLBACK");
    return r;
  }
  return exec("COMMIT");
}

}  // namespace repd
