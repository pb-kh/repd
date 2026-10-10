// Deterministic privilege classification per step (research R12).
#pragma once

#include <optional>
#include <string>

#include "plan/step.hpp"
#include "platform/paths.hpp"
#include "util/errors.hpp"

namespace repd {

// files: user if dest is under the target home, else system; scope may only
// confirm, or set "user" for a non-home path; scope="system" under home is an error.
Result<Mode> file_privilege(const Paths& paths, const std::string& logical_dest,
                            const std::optional<std::string>& scope, const std::string& module);
Mode service_privilege(const std::string& scope);
Mode action_privilege(const std::string& mode);

}  // namespace repd
