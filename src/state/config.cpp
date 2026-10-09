#include <repro/config.hpp>

namespace repro {

ConfigRepository ConfigRepository::at(const std::string& root) {
  ConfigRepository r;
  r.root = root;
  r.entry = root + "/repro.lua";
  r.lock = root + "/repro.lock.lua";
  r.modules_dir = root + "/modules";
  r.lua_plugins_dir = root + "/plugins/lua";
  return r;
}

}  // namespace repro
