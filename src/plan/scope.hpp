// Module names (data-model.md "ModuleName"): ^[a-z0-9_+-]+(/[a-z0-9_+-]+)*$
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace repro {

bool valid_module_name(std::string_view name);
// Path relative to modules/ ("dev/git.lua", "dev/git/init.lua") -> module name, or "".
std::string module_name_from_path(std::string_view rel);
// "dev/c++" -> "dev--c++" (environment.d file slug, research R13).
std::string module_slug(std::string_view name);
// E_UNKNOWN hint text for a missing module, or "".
std::string suggest_module(std::string_view name, std::span<const std::string> known);

}  // namespace repro
