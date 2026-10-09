#include "cli/output.hpp"

#include <unistd.h>

#include <cstdlib>
#include <format>
#include <print>

namespace repro {

Style Style::detect(int fd, bool no_color_flag) {
  Style s;
  s.color = !no_color_flag && std::getenv("NO_COLOR") == nullptr && ::isatty(fd) == 1;
  return s;
}

std::string Style::paint(std::string_view text, std::string_view sgr) const {
  if (!color) return std::string(text);
  return std::format("\033[{}m{}\033[0m", sgr, text);
}

void print_warning(std::string_view msg) {
  const Style s = Style::detect(2, false);
  std::println(stderr, "{}: {}", s.yellow("warning"), msg);
}

}  // namespace repro
