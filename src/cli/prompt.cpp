#include "cli/prompt.hpp"

#include <unistd.h>

#include <cstdio>
#include <format>

#include "util/strings.hpp"

namespace repd {

bool can_prompt(bool yes) { return !yes && ::isatty(0) == 1 && ::isatty(2) == 1; }

Result<std::string> prompt_provider(const std::string& iface,
                                    const std::vector<const ModuleMeta*>& candidates,
                                    const std::vector<std::string>& requirers) {
  std::FILE* tty = std::fopen("/dev/tty", "r+");
  if (tty == nullptr) {
    return fail(ec::kAmbiguous, std::format("interface {} needs a provider and no terminal is available", iface),
                std::format("pass --provider {}=<module>", iface));
  }
  std::size_t width = 0;
  for (const auto* c : candidates) width = std::max(width, c->name.size());
  std::string text = std::format("\ninterface {} needs a provider{}:\n", iface,
                                 requirers.empty() ? "" : " (required by " + join(requirers, ", ") + ")");
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto* c = candidates[i];
    text += std::format("  {}) {:<{}}  {}{}\n", i + 1, c->name, width, c->description,
                        i == 0 ? "  [default]" : "");
  }
  std::string chosen;
  for (int attempt = 0; attempt < 3 && chosen.empty(); ++attempt) {
    std::fputs(text.c_str(), tty);
    std::fputs(std::format("choose [1-{}, default 1]: ", candidates.size()).c_str(), tty);
    std::fflush(tty);
    char buf[256];
    if (std::fgets(buf, sizeof buf, tty) == nullptr) break;
    const std::string answer(trim(buf));
    if (answer.empty()) {
      chosen = candidates.front()->name;
      break;
    }
    char* end = nullptr;
    const long n = std::strtol(answer.c_str(), &end, 10);
    if (end != nullptr && *end == '\0' && n >= 1 && static_cast<std::size_t>(n) <= candidates.size()) {
      chosen = candidates[static_cast<std::size_t>(n) - 1]->name;
      break;
    }
    for (const auto* c : candidates) {
      if (c->name == answer) chosen = c->name;
    }
    if (chosen.empty()) std::fputs(std::format("'{}' is not one of the choices\n", answer).c_str(), tty);
  }
  std::fclose(tty);
  if (chosen.empty()) {
    return fail(ec::kAmbiguous, std::format("no provider chosen for interface {}", iface),
                std::format("pass --provider {}={}", iface, candidates.front()->name));
  }
  return chosen;
}

}  // namespace repd
