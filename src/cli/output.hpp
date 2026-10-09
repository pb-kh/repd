// Terminal output helpers (research R19): color only on a TTY and without NO_COLOR.
#pragma once

#include <string>
#include <string_view>

namespace repro {

struct Style {
  bool color = false;
  static Style detect(int fd, bool no_color_flag);
  std::string paint(std::string_view text, std::string_view sgr) const;
  std::string green(std::string_view t) const { return paint(t, "32"); }
  std::string yellow(std::string_view t) const { return paint(t, "33"); }
  std::string red(std::string_view t) const { return paint(t, "1;31"); }
  std::string dim(std::string_view t) const { return paint(t, "2"); }
  std::string bold(std::string_view t) const { return paint(t, "1"); }
};

void print_warning(std::string_view msg);

}  // namespace repro
