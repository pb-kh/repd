#include "apply/signals.hpp"

#include <signal.h>

#include <atomic>

#include "platform/proc.hpp"

namespace repro {
namespace {

std::atomic<bool> g_interrupted{false};

extern "C" void on_signal(int sig) {
  g_interrupted.store(true);
  if (const int child = current_child_pid(); child > 0) ::kill(child, sig);
}

}  // namespace

void install_signal_handlers() {
  struct sigaction sa{};
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // no SA_RESTART: interrupt blocking waits promptly
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGTERM, &sa, nullptr);
}

bool interrupted() { return g_interrupted.load(); }

}  // namespace repro
