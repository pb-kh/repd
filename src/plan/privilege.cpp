#include "plan/privilege.hpp"

#include <format>

namespace repro {

Result<Mode> file_privilege(const Paths& paths, const std::string& logical_dest,
                            const std::optional<std::string>& scope, const std::string& module) {
  const bool home = paths.under_home(logical_dest);
  if (scope == "system" && home) {
    return fail(ec::kConfig,
                std::format("module {}: {} is in the home directory but has scope = \"system\"",
                            module, paths.display(logical_dest)),
                "remove scope (files under ~ belong to the user pass)");
  }
  if (scope == "user") return Mode::User;
  return home ? Mode::User : Mode::System;
}

Mode service_privilege(const std::string& scope) {
  return scope == "user" ? Mode::User : Mode::System;
}

Mode action_privilege(const std::string& mode) { return mode == "system" ? Mode::System : Mode::User; }

}  // namespace repro
