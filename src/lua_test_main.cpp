// repd-lua-test: runs a luaunit test file inside the same LuaJIT host as the
// repd binary (embedded bootstrap + "repd.host"), plus vendored luaunit.
// Usage: repd-lua-test TEST.lua [args...]
#include <print>

#include "eval/host.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"

namespace repd {
std::span<const EmbeddedFile> luaunit_files();
}

int main(int argc, char** argv) {
  using namespace repd;
  if (argc < 2) {
    std::println(stderr, "usage: repd-lua-test TEST.lua [args...]");
    return 2;
  }
  auto paths = Paths::detect(Env::from_process());
  if (!paths) {
    print_error(paths.error());
    return 2;
  }
  RealFs fs;
  LuaHost host(*paths, fs, Mode::User, luaunit_files());
  lua_State* L = host.state();
  // Expose the remaining arguments as the global `arg` table (arg[0] = script).
  lua_createtable(L, argc, 0);
  for (int i = 1; i < argc; ++i) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i - 1);
  }
  lua_setglobal(L, "arg");
  if (luaL_loadfile(L, argv[1]) != 0 || lua_pcall(L, 0, 0, 0) != 0) {
    const char* msg = lua_tostring(L, -1);
    std::println(stderr, "{}", msg != nullptr ? msg : "(error object is not a string)");
    return 1;
  }
  return 0;
}
