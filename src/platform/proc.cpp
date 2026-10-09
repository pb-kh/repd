#include "platform/proc.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <format>

extern char** environ;

namespace repro {
namespace {

std::atomic<int> g_child{0};

void close_fd(int& fd) {
  if (fd >= 0) ::close(fd);
  fd = -1;
}

}  // namespace

int current_child_pid() { return g_child.load(); }

std::string tail(const std::string& s, std::size_t max) {
  return s.size() <= max ? s : s.substr(s.size() - max);
}

Result<ProcResult> run_process(const std::vector<std::string>& argv, const ProcOptions& opts) {
  if (argv.empty()) return fail(ec::kStep, "empty command", "this is a bug in repro");
  int in_pipe[2] = {-1, -1}, out_pipe[2] = {-1, -1}, err_pipe[2] = {-1, -1};
  if ((!opts.inherit_input && ::pipe2(in_pipe, O_CLOEXEC) != 0) ||
      (!opts.inherit_output &&
       (::pipe2(out_pipe, O_CLOEXEC) != 0 || ::pipe2(err_pipe, O_CLOEXEC) != 0))) {
    return fail(ec::kStep, std::format("cannot create pipes: {}", std::strerror(errno)), "");
  }
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  if (!opts.inherit_input) posix_spawn_file_actions_adddup2(&fa, in_pipe[0], 0);
  if (!opts.inherit_output) {
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], 1);
    posix_spawn_file_actions_adddup2(&fa, err_pipe[1], 2);
  }
  std::vector<char*> cargv;
  for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
  cargv.push_back(nullptr);
  pid_t pid = 0;
  const int rc = ::posix_spawnp(&pid, cargv[0], &fa, nullptr, cargv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  close_fd(in_pipe[0]);
  close_fd(out_pipe[1]);
  close_fd(err_pipe[1]);
  if (rc != 0) {
    close_fd(in_pipe[1]);
    close_fd(out_pipe[0]);
    close_fd(err_pipe[0]);
    return fail(ec::kStep, std::format("cannot run {}: {}", argv[0], std::strerror(rc)),
                std::format("make sure '{}' is installed and on PATH", argv[0]));
  }
  g_child.store(pid);

  ProcResult result;
  std::size_t in_off = 0;
  if (opts.stdin_data.empty() || opts.inherit_input) close_fd(in_pipe[1]);
  while (in_pipe[1] >= 0 || out_pipe[0] >= 0 || err_pipe[0] >= 0) {
    pollfd fds[3];
    nfds_t n = 0;
    if (in_pipe[1] >= 0) fds[n++] = {in_pipe[1], POLLOUT, 0};
    if (out_pipe[0] >= 0) fds[n++] = {out_pipe[0], POLLIN, 0};
    if (err_pipe[0] >= 0) fds[n++] = {err_pipe[0], POLLIN, 0};
    if (::poll(fds, n, -1) < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (nfds_t i = 0; i < n; ++i) {
      if (fds[i].revents == 0) continue;
      if (fds[i].fd == in_pipe[1]) {
        const ssize_t w = ::write(in_pipe[1], opts.stdin_data.data() + in_off,
                                  opts.stdin_data.size() - in_off);
        if (w > 0) in_off += static_cast<std::size_t>(w);
        if (w < 0 || in_off == opts.stdin_data.size()) close_fd(in_pipe[1]);
        continue;
      }
      char buf[8192];
      const ssize_t r = ::read(fds[i].fd, buf, sizeof buf);
      if (r <= 0) {
        if (r < 0 && errno == EINTR) continue;
        if (fds[i].fd == out_pipe[0]) close_fd(out_pipe[0]);
        else close_fd(err_pipe[0]);
        continue;
      }
      (fds[i].fd == out_pipe[0] ? result.out : result.err).append(buf, static_cast<std::size_t>(r));
    }
  }
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  g_child.store(0);
  result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  return result;
}

Result<ProcResult> run_shell(const std::string& cmd, const ProcOptions& opts) {
  return run_process({"/bin/sh", "-c", cmd}, opts);
}

}  // namespace repro
