#include "state/run_lock.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>

namespace repd {

RunLock& RunLock::operator=(RunLock&& o) noexcept {
  if (this != &o) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = o.fd_;
    o.fd_ = -1;
  }
  return *this;
}

RunLock::~RunLock() {
  if (fd_ >= 0) ::close(fd_);  // closing releases the flock
}

Result<RunLock> RunLock::acquire(const std::string& state_dir, Mode mode) {
  RealFs fs;
  if (auto r = fs.mkdirs(state_dir, 0700); !r) return std::unexpected(r.error());
  const std::string path = state_dir + "/run.lock";
  const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) {
    return fail(ec::kIo, std::format("cannot open {}: {}", path, std::strerror(errno)),
                mode == Mode::System ? "run: sudo repd sync -s" : "check the state directory");
  }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    char buf[32] = {};
    const ssize_t n = ::pread(fd, buf, sizeof buf - 1, 0);
    ::close(fd);
    const std::string pid = n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : "?";
    return fail(ec::kBusy,
                std::format("another repd run is active in {} mode (pid {})", to_string(mode),
                            pid.substr(0, pid.find('\n'))),
                "wait for it to finish, then run again");
  }
  const std::string pid = std::to_string(::getpid()) + "\n";
  if (::ftruncate(fd, 0) == 0) (void)::pwrite(fd, pid.data(), pid.size(), 0);
  return RunLock(fd);
}

}  // namespace repd
