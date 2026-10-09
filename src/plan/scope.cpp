#include "plan/scope.hpp"

#include <format>

#include "util/strings.hpp"

namespace repro {

bool valid_module_name(std::string_view name) {
  if (name.empty()) return false;
  for (const auto& part : split(name, '/', /*keep_empty=*/true)) {
    if (part.empty()) return false;
    for (char c : part) {
      const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '+' ||
                      c == '-';
      if (!ok) return false;
    }
  }
  return true;
}

std::string module_name_from_path(std::string_view rel) {
  if (!rel.ends_with(".lua")) return {};
  std::string_view stem = rel.substr(0, rel.size() - 4);
  if (stem == "init") return {};
  if (stem.ends_with("/init")) stem.remove_suffix(5);
  return std::string(stem);
}

std::string module_slug(std::string_view name) { return replace_all(std::string(name), "/", "--"); }

std::string suggest_module(std::string_view name, std::span<const std::string> known) {
  const std::string c = closest_match(name, known);
  return c.empty() ? std::string{} : std::format("did you mean '{}'?", c);
}

}  // namespace repro
