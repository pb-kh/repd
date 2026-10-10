// Module discovery (data-model.md "Module"): every modules/**.lua file is a
// module named by its path; modules/x/init.lua names the directory.
#pragma once

#include <map>
#include <string>

#include <repd/config.hpp>

#include "platform/fs.hpp"
#include "util/errors.hpp"

namespace repd {

struct DiscoveredModule {
  std::string name;
  std::string path;  // absolute
  std::string rel;   // relative to the config root, e.g. "modules/dev/git.lua"
};

// Fails with E_CONFIG if repd.lua is missing.
Status check_repository(const Fs& fs, const ConfigRepository& repo);
Result<std::map<std::string, DiscoveredModule>> discover_modules(const Fs& fs,
                                                                 const ConfigRepository& repo);
// plugins/lua/*.lua, sorted by name.
std::vector<DiscoveredModule> discover_lua_plugins(const Fs& fs, const ConfigRepository& repo);

}  // namespace repd
