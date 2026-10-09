// Configuration repository layout (data-model.md "ConfigRepository", research R12).
#pragma once

#include <string>

namespace repro {

struct ConfigRepository {
  std::string root;             // absolute
  std::string entry;            // <root>/repro.lua (must exist)
  std::string lock;             // <root>/repro.lock.lua (optional, generated)
  std::string modules_dir;      // <root>/modules
  std::string lua_plugins_dir;  // <root>/plugins/lua

  static ConfigRepository at(const std::string& root);
};

}  // namespace repro
