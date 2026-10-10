#include "util/errors.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <format>
#include <print>

namespace repd {

int Error::exit_code() const {
  if (code == ec::kBlocked || code == ec::kStep) return kExitApplyFailed;
  if (code == ec::kInterrupted) return kExitInterrupted;
  return kExitConfig;
}

std::string format_error(const Error& e, bool color) {
  const char* red = color ? "\033[1;31m" : "";
  const char* dim = color ? "\033[2m" : "";
  const char* reset = color ? "\033[0m" : "";
  std::string out = std::format("{}error[{}]{}: {}\n", red, e.code, reset, e.message);
  if (!e.hint.empty()) out += std::format("  {}hint:{} {}\n", dim, reset, e.hint);
  return out;
}

void print_error(const Error& e) {
  const bool color = ::isatty(STDERR_FILENO) == 1 && std::getenv("NO_COLOR") == nullptr;
  std::print(stderr, "{}", format_error(e, color));
}

std::string did_you_mean(std::string_view candidate) {
  if (candidate.empty()) return {};
  return std::format("did you mean '{}'?", candidate);
}

}  // namespace repd
