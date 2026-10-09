// Per-mode SQLite store (research R5): WAL, synchronous=NORMAL, foreign keys,
// numbered migrations applied in one transaction on open.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "util/errors.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace repro {

class Stmt {
 public:
  Stmt() = default;
  explicit Stmt(sqlite3_stmt* s) : s_(s) {}
  ~Stmt();
  Stmt(Stmt&& o) noexcept : s_(o.s_) { o.s_ = nullptr; }
  Stmt& operator=(Stmt&& o) noexcept;
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  Stmt& bind(int i, std::string_view v);
  Stmt& bind(int i, const std::string& v) { return bind(i, std::string_view(v)); }
  Stmt& bind(int i, const char* v) { return bind(i, std::string_view(v)); }
  Stmt& bind(int i, std::int64_t v);
  Stmt& bind(int i, std::nullopt_t);
  Stmt& bind(int i, const std::optional<std::string>& v);

  // Returns true while a row is available; false at the end.
  Result<bool> step();
  Status run();  // step to completion

  std::string text(int col) const;
  std::optional<std::string> opt_text(int col) const;
  std::int64_t integer(int col) const;

 private:
  sqlite3_stmt* s_ = nullptr;
};

class Db {
 public:
  static constexpr int kSchemaVersion = 1;
  enum class Access { ReadWrite, ReadOnly };

  // ReadWrite creates the directory (0700) and file (0600) and migrates.
  // ReadOnly never creates anything: a missing file yields a closed Db.
  static Result<Db> open(const std::string& path, Access access);

  Db() = default;
  ~Db();
  Db(Db&& o) noexcept : db_(o.db_), path_(std::move(o.path_)) { o.db_ = nullptr; }
  Db& operator=(Db&& o) noexcept;
  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;

  bool is_open() const { return db_ != nullptr; }
  const std::string& path() const { return path_; }

  Status exec(std::string_view sql);
  Result<Stmt> prepare(std::string_view sql);
  std::int64_t last_insert_id() const;
  int schema_version();

 private:
  Status migrate();
  Error error(std::string_view what) const;

  sqlite3* db_ = nullptr;
  std::string path_;
};

}  // namespace repro
