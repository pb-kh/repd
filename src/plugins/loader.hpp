// Native plugin discovery and loading (contracts/plugin-abi.md): dlopen with
// RTLD_NOW|RTLD_LOCAL, ABI check, fail-soft per plugin.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "plugins/backends.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"

namespace repd {

// Loads every *.so in the mode's search dirs into `registry` (refusals recorded there).
void load_native_plugins(const Fs& fs, const Paths& paths, Mode mode, BackendRegistry& registry);

// Plugins are named librepd-<name>.so; other files in the directory are ignored.
bool is_plugin_file(std::string_view name);

// What api->option() answers from: declared options and the effective values
// (repd.lua plus --option overrides). Both must outlive plugin calls.
void set_plugin_options(const BackendRegistry* registry, const std::map<std::string, std::string>* options);

// Log sink for plugin api->log() (the current step's journal detail).
void set_plugin_log_sink(std::string* sink);

}  // namespace repd
