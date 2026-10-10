// Filesystem abstraction (research R20). All mutation goes through here so
// unit tests can substitute an in-memory fake and integration tests can
// redirect system paths via the sysroot hook (see Paths::physical).
// Paths passed to Fs are *physical* paths.
#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/errors.hpp"

namespace repd {

enum class FileType { None, File, Dir, Symlink, Other };

struct FileStat {
  FileType type = FileType::None;
  mode_t mode = 0;  // permission bits only
  off_t size = 0;
  uid_t uid = 0;
  gid_t gid = 0;
  bool exists() const { return type != FileType::None; }
};

class Fs {
 public:
  virtual ~Fs() = default;

  virtual FileStat lstat(const std::string& path) const = 0;
  virtual FileStat stat(const std::string& path) const = 0;  // follows symlinks
  virtual Result<std::string> read(const std::string& path,
                                   std::size_t max_bytes = 64u << 20) const = 0;
  virtual Result<std::string> readlink(const std::string& path) const = 0;
  virtual Result<std::vector<std::string>> list(const std::string& dir) const = 0;  // sorted

  // tmp + fsync + rename in the destination directory.
  virtual Status write_atomic(const std::string& path, std::string_view data,
                              std::optional<mode_t> mode = std::nullopt) = 0;
  // Creates/replaces `path` as a symlink to `target` atomically (tmp link + rename).
  virtual Status symlink_atomic(const std::string& target, const std::string& path) = 0;
  virtual Status mkdirs(const std::string& path, mode_t mode = 0755) = 0;
  virtual Status chmod(const std::string& path, mode_t mode) = 0;
  virtual Status chown(const std::string& path, uid_t uid, gid_t gid) = 0;
  virtual Status rename(const std::string& from, const std::string& to) = 0;
  virtual Status remove(const std::string& path) = 0;  // file or symlink
};

class RealFs final : public Fs {
 public:
  FileStat lstat(const std::string& path) const override;
  FileStat stat(const std::string& path) const override;
  Result<std::string> read(const std::string& path,
                           std::size_t max_bytes = 64u << 20) const override;
  Result<std::string> readlink(const std::string& path) const override;
  Result<std::vector<std::string>> list(const std::string& dir) const override;
  Status write_atomic(const std::string& path, std::string_view data,
                      std::optional<mode_t> mode = std::nullopt) override;
  Status symlink_atomic(const std::string& target, const std::string& path) override;
  Status mkdirs(const std::string& path, mode_t mode = 0755) override;
  Status chmod(const std::string& path, mode_t mode) override;
  Status chown(const std::string& path, uid_t uid, gid_t gid) override;
  Status rename(const std::string& from, const std::string& to) override;
  Status remove(const std::string& path) override;
};

}  // namespace repd
