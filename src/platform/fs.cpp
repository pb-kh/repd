#include "platform/fs.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <format>

#include "util/strings.hpp"

namespace repro {
namespace {

std::unexpected<Error> io_error(std::string_view what, const std::string& path, int err) {
  return fail(ec::kIo, std::format("cannot {} {}: {}", what, path, std::strerror(err)),
              "check the path's permissions and that its parent directory exists");
}

FileStat from_stat(const struct stat& st) {
  FileStat out;
  if (S_ISREG(st.st_mode)) out.type = FileType::File;
  else if (S_ISDIR(st.st_mode)) out.type = FileType::Dir;
  else if (S_ISLNK(st.st_mode)) out.type = FileType::Symlink;
  else out.type = FileType::Other;
  out.mode = st.st_mode & 07777;
  out.size = st.st_size;
  out.uid = st.st_uid;
  out.gid = st.st_gid;
  return out;
}

std::string temp_sibling(const std::string& path) {
  return std::format("{}/.{}.repro-tmp-{}", dirname(path), basename(path), ::getpid());
}

}  // namespace

FileStat RealFs::lstat(const std::string& path) const {
  struct stat st{};
  if (::lstat(path.c_str(), &st) != 0) return {};
  return from_stat(st);
}

FileStat RealFs::stat(const std::string& path) const {
  struct stat st{};
  if (::stat(path.c_str(), &st) != 0) return {};
  return from_stat(st);
}

Result<std::string> RealFs::read(const std::string& path, std::size_t max_bytes) const {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return io_error("read", path, errno);
  std::string out;
  char buf[65536];
  for (;;) {
    const ssize_t n = ::read(fd, buf, sizeof buf);
    if (n < 0) {
      if (errno == EINTR) continue;
      const int err = errno;
      ::close(fd);
      return io_error("read", path, err);
    }
    if (n == 0) break;
    out.append(buf, static_cast<std::size_t>(n));
    if (out.size() > max_bytes) {
      ::close(fd);
      return fail(ec::kIo, std::format("{} is larger than {} bytes", path, max_bytes),
                  "manage large files with a copy step instead of reading them in Lua");
    }
  }
  ::close(fd);
  return out;
}

Result<std::string> RealFs::readlink(const std::string& path) const {
  std::string buf(4096, '\0');
  const ssize_t n = ::readlink(path.c_str(), buf.data(), buf.size());
  if (n < 0) return io_error("read link", path, errno);
  buf.resize(static_cast<std::size_t>(n));
  return buf;
}

Result<std::vector<std::string>> RealFs::list(const std::string& dir) const {
  DIR* d = ::opendir(dir.c_str());
  if (d == nullptr) return io_error("list", dir, errno);
  std::vector<std::string> names;
  while (const dirent* e = ::readdir(d)) {
    const std::string_view name = e->d_name;
    if (name == "." || name == "..") continue;
    names.emplace_back(name);
  }
  ::closedir(d);
  std::ranges::sort(names);
  return names;
}

Status RealFs::write_atomic(const std::string& path, std::string_view data,
                            std::optional<mode_t> mode) {
  const std::string tmp = temp_sibling(path);
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode.value_or(0644));
  if (fd < 0) return io_error("write", path, errno);
  std::size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      const int err = errno;
      ::close(fd);
      ::unlink(tmp.c_str());
      return io_error("write", path, err);
    }
    off += static_cast<std::size_t>(n);
  }
  if (mode && ::fchmod(fd, *mode) != 0) {
    const int err = errno;
    ::close(fd);
    ::unlink(tmp.c_str());
    return io_error("chmod", path, err);
  }
  if (::fsync(fd) != 0 || ::close(fd) != 0) {
    const int err = errno;
    ::unlink(tmp.c_str());
    return io_error("sync", path, err);
  }
  if (::rename(tmp.c_str(), path.c_str()) != 0) {
    const int err = errno;
    ::unlink(tmp.c_str());
    return io_error("replace", path, err);
  }
  // Persist the rename itself.
  const int dfd = ::open(dirname(path).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd >= 0) {
    ::fsync(dfd);
    ::close(dfd);
  }
  return {};
}

Status RealFs::symlink_atomic(const std::string& target, const std::string& path) {
  const std::string tmp = temp_sibling(path);
  ::unlink(tmp.c_str());
  if (::symlink(target.c_str(), tmp.c_str()) != 0) return io_error("create link", path, errno);
  if (::rename(tmp.c_str(), path.c_str()) != 0) {
    const int err = errno;
    ::unlink(tmp.c_str());
    return io_error("replace", path, err);
  }
  return {};
}

Status RealFs::mkdirs(const std::string& path, mode_t mode) {
  if (path.empty() || path == "/") return {};
  const FileStat st = stat(path);
  if (st.type == FileType::Dir) return {};
  if (st.exists()) return fail(ec::kIo, std::format("{} exists and is not a directory", path),
                               "move the file out of the way");
  if (auto r = mkdirs(dirname(path), mode); !r) return r;
  if (::mkdir(path.c_str(), mode) != 0 && errno != EEXIST) {
    return io_error("create directory", path, errno);
  }
  return {};
}

Status RealFs::chmod(const std::string& path, mode_t mode) {
  if (::chmod(path.c_str(), mode) != 0) return io_error("chmod", path, errno);
  return {};
}

Status RealFs::chown(const std::string& path, uid_t uid, gid_t gid) {
  if (::lchown(path.c_str(), uid, gid) != 0) return io_error("chown", path, errno);
  return {};
}

Status RealFs::rename(const std::string& from, const std::string& to) {
  if (::rename(from.c_str(), to.c_str()) != 0) return io_error("rename", from, errno);
  return {};
}

Status RealFs::remove(const std::string& path) {
  if (::unlink(path.c_str()) != 0 && errno != ENOENT) return io_error("remove", path, errno);
  return {};
}

}  // namespace repro
