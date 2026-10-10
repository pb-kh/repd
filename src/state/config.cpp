#include <repd/config.hpp>

namespace repd {

ConfigRepository ConfigRepository::at(const std::string& root) {
  ConfigRepository r;
  r.root = root;
  r.entry = root + "/repd.lua";
  r.lock = root + "/repd.lock.lua";
  r.modules_dir = root + "/modules";
  r.lua_plugins_dir = root + "/plugins/lua";
  return r;
}

}  // namespace repd
