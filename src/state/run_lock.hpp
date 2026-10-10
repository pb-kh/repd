// Same-mode concurrency guard (research R6): flock(LOCK_EX|LOCK_NB) on
// <state-dir>/run.lock, taken before planning a mutating sync.
#pragma once

#include <string>

#include "platform/paths.hpp"
#include "util/errors.hpp"

namespace repd {

class RunLock {
 public:
  static Result<RunLock> acquire(const std::string& state_dir, Mode mode);
  RunLock(RunLock&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
  RunLock& operator=(RunLock&& o) noexcept;
  RunLock(const RunLock&) = delete;
  RunLock& operator=(const RunLock&) = delete;
  ~RunLock();

 private:
  explicit RunLock(int fd) : fd_(fd) {}
  int fd_ = -1;
};

}  // namespace repd
