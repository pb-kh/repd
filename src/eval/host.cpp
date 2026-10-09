#include "eval/host.hpp"

#include <sys/utsname.h>
#include <unistd.h>

#include <chrono>
#include <exception>
#include <format>

#include "eval/bridge.hpp"
#include "platform/proc.hpp"
#include "util/sha256.hpp"
#include "util/strings.hpp"

namespace repro {

std::string_view to_string(Phase p) {
  switch (p) {
    case Phase::Eval: return "EVAL";
    case Phase::Check: return "CHECK";
    case Phase::Apply: return "APPLY";
  }
  return "EVAL";
}

std::string utc_now() {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

namespace {

const char kHostKey = 'h';

LuaHost& host_of(lua_State* L) {
  lua_pushlightuserdata(L, const_cast<char*>(&kHostKey));
  lua_rawget(L, LUA_REGISTRYINDEX);
  auto* host = static_cast<LuaHost*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return *host;
}

// Raises a structured repro.err error from C.
[[noreturn]] void raise(lua_State* L, const Error& e) {
  lua_createtable(L, 0, 3);
  lua_pushlstring(L, e.code.data(), e.code.size());
  lua_setfield(L, -2, "code");
  lua_pushlstring(L, e.message.data(), e.message.size());
  lua_setfield(L, -2, "message");
  lua_pushlstring(L, e.hint.data(), e.hint.size());
  lua_setfield(L, -2, "hint");
  // Attach the repro.err metatable so err.is() recognises it.
  lua_getglobal(L, "require");
  lua_pushliteral(L, "repro.err");
  if (lua_pcall(L, 1, 1, 0) == 0) {
    lua_getfield(L, -1, "new");
    lua_pushliteral(L, "");
    lua_pushliteral(L, "");
    if (lua_pcall(L, 2, 1, 0) == 0) {
      lua_getmetatable(L, -1);
      lua_setmetatable(L, -4);
    }
    lua_pop(L, 2);
  } else {
    lua_pop(L, 1);
  }
  lua_error(L);
  std::abort();  // unreachable
}

// Wraps a host function so C++ exceptions never unwind through Lua frames.
template <int (*F)(lua_State*)>
int guarded(lua_State* L) {
  std::string message;
  try {
    return F(L);
  } catch (const std::exception& ex) {
    message = ex.what();
  }
  return luaL_error(L, "internal error: %s", message.c_str());
}

std::string check_string(lua_State* L, int i) {
  std::size_t n = 0;
  const char* s = luaL_checklstring(L, i, &n);
  return {s, n};
}

std::string logical_path(lua_State* L, int i) {
  LuaHost& host = host_of(L);
  std::string p = host.paths().expand(check_string(L, i));
  if (!p.starts_with('/')) {
    raise(L, Error{std::string(ec::kConfig),
                   std::format("rp.fs expects an absolute or ~/ path (got '{}')", p),
                   "write \"~/" + p + "\" or an absolute path"});
  }
  return normalize_path(p);
}

int h_sha256(lua_State* L) {
  const std::string s = check_string(L, 1);
  const std::string hex = sha256_hex(s);
  lua_pushlstring(L, hex.data(), hex.size());
  return 1;
}

int h_fs_exists(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string p = host.paths().physical(logical_path(L, 1));
  lua_pushboolean(L, host.fs().lstat(p).exists() ? 1 : 0);
  return 1;
}

int h_fs_read(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string p = host.paths().physical(logical_path(L, 1));
  if (host.fs().stat(p).type != FileType::File) {
    lua_pushnil(L);
    return 1;
  }
  auto data = host.fs().read(p, 1u << 20);
  if (!data) raise(L, data.error());
  lua_pushlstring(L, data->data(), data->size());
  return 1;
}

int h_fs_stat(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string p = host.paths().physical(logical_path(L, 1));
  FileStat st = host.fs().stat(p);
  if (!st.exists()) {
    st = host.fs().lstat(p);
    if (!st.exists()) {
      lua_pushnil(L);
      return 1;
    }
  }
  const char* type = st.type == FileType::File      ? "file"
                     : st.type == FileType::Dir     ? "dir"
                     : st.type == FileType::Symlink ? "link"
                                                    : "other";
  lua_createtable(L, 0, 5);
  lua_pushstring(L, type);
  lua_setfield(L, -2, "type");
  lua_pushnumber(L, st.mode);
  lua_setfield(L, -2, "mode");
  lua_pushnumber(L, static_cast<double>(st.size));
  lua_setfield(L, -2, "size");
  lua_pushnumber(L, st.uid);
  lua_setfield(L, -2, "uid");
  lua_pushnumber(L, st.gid);
  lua_setfield(L, -2, "gid");
  return 1;
}

int h_fs_list(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string p = host.paths().physical(logical_path(L, 1));
  auto names = host.fs().list(p);
  lua_newtable(L);
  if (!names) return 1;
  for (std::size_t i = 0; i < names->size(); ++i) {
    lua_pushstring(L, (*names)[i].c_str());
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  return 1;
}

int h_read_file(lua_State* L) {
  LuaHost& host = host_of(L);
  auto data = host.fs().read(check_string(L, 1));
  if (!data) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushlstring(L, data->data(), data->size());
  return 1;
}

int h_write_file(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string path = check_string(L, 1);
  const std::string data = check_string(L, 2);
  if (auto r = host.fs().mkdirs(dirname(path), 0700); !r) raise(L, r.error());
  if (auto r = host.fs().write_atomic(path, data, 0600); !r) raise(L, r.error());
  return 0;
}

int h_sys_hostname(lua_State* L) {
  char buf[256] = {};
  ::gethostname(buf, sizeof buf - 1);
  lua_pushstring(L, buf);
  return 1;
}

int h_sys_user(lua_State* L) {
  lua_pushstring(L, host_of(L).paths().user().name.c_str());
  return 1;
}

int h_sys_home(lua_State* L) {
  lua_pushstring(L, host_of(L).paths().user().home.c_str());
  return 1;
}

int h_sys_arch(lua_State* L) {
  utsname u{};
  ::uname(&u);
  lua_pushstring(L, u.machine);
  return 1;
}

int h_sys_os_release(lua_State* L) {
  LuaHost& host = host_of(L);
  auto text = host.fs().read(host.paths().physical("/etc/os-release"));
  if (!text) text = host.fs().read(host.paths().physical("/usr/lib/os-release"));
  lua_newtable(L);
  if (!text) return 1;
  for (const auto& line : split(*text, '\n')) {
    const auto eq = line.find('=');
    if (eq == std::string::npos || line.starts_with('#')) continue;
    std::string value(trim(std::string_view(line).substr(eq + 1)));
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front()) {
      value = value.substr(1, value.size() - 2);
    }
    lua_pushstring(L, value.c_str());
    lua_setfield(L, -2, line.substr(0, eq).c_str());
  }
  return 1;
}

int h_phase(lua_State* L) {
  const auto p = to_string(host_of(L).phase());
  lua_pushlstring(L, p.data(), p.size());
  return 1;
}

int h_mode(lua_State* L) {
  const auto m = to_string(host_of(L).mode());
  lua_pushlstring(L, m.data(), m.size());
  return 1;
}

int h_log(lua_State* L) {
  host_of(L).log(check_string(L, 1));
  return 0;
}

int h_exec(lua_State* L) {
  LuaHost& host = host_of(L);
  if (host.phase() != Phase::Apply) {
    raise(L, Error{std::string(ec::kDeclarative), "commands can only run while applying",
                   "move the command into an action's run function"});
  }
  const std::string cmd = check_string(L, 1);
  auto r = run_shell(cmd);
  if (!r) raise(L, r.error());
  host.log(std::format("$ {}\n{}{}", cmd, tail(r->out, 2048), tail(r->err, 2048)));
  lua_createtable(L, 0, 3);
  lua_pushnumber(L, r->code);
  lua_setfield(L, -2, "code");
  lua_pushlstring(L, r->out.data(), r->out.size());
  lua_setfield(L, -2, "stdout");
  lua_pushlstring(L, r->err.data(), r->err.size());
  lua_setfield(L, -2, "stderr");
  return 1;
}

int h_backend_call(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string ns = check_string(L, 1);
  const std::string op = check_string(L, 2);
  List args;
  for (int i = 3; i <= lua_gettop(L); ++i) {
    auto v = to_value(L, i);
    if (!v) raise(L, v.error());
    args.push_back(std::move(*v));
  }
  if (!host.backend_dispatch()) {
    raise(L, Error{std::string(ec::kNoBackend), std::format("no {} backend is available", ns),
                   "install a backend add-on (e.g. repro-pacman)"});
  }
  auto r = host.backend_dispatch()(ns, op, args);
  if (!r) raise(L, r.error());
  push_value(L, *r);
  return 1;
}

int h_backend_mem_path(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string name = check_string(L, 1);
  const std::string priv = check_string(L, 2);
  const Mode m = priv == "user" ? Mode::User : Mode::System;
  const std::string p = host.paths().state_dir(m) + "/lua-backend-mem/" + name + ".lua";
  lua_pushstring(L, p.c_str());
  return 1;
}

int h_utc_now(lua_State* L) {
  lua_pushstring(L, utc_now().c_str());
  return 1;
}

int open_host_module(lua_State* L) {
  static const luaL_Reg kFuncs[] = {
      {"sha256", guarded<h_sha256>},
      {"fs_exists", guarded<h_fs_exists>},
      {"fs_read", guarded<h_fs_read>},
      {"fs_stat", guarded<h_fs_stat>},
      {"fs_list", guarded<h_fs_list>},
      {"read_file", guarded<h_read_file>},
      {"write_file", guarded<h_write_file>},
      {"sys_hostname", guarded<h_sys_hostname>},
      {"sys_user", guarded<h_sys_user>},
      {"sys_home", guarded<h_sys_home>},
      {"sys_arch", guarded<h_sys_arch>},
      {"sys_os_release", guarded<h_sys_os_release>},
      {"phase", guarded<h_phase>},
      {"mode", guarded<h_mode>},
      {"log", guarded<h_log>},
      {"exec", guarded<h_exec>},
      {"backend_call", guarded<h_backend_call>},
      {"backend_mem_path", guarded<h_backend_mem_path>},
      {"utc_now", guarded<h_utc_now>},
      {nullptr, nullptr},
  };
  lua_newtable(L);
  luaL_register(L, nullptr, kFuncs);
  return 1;
}

// package.loaders entry resolving embedded modules.
int embedded_searcher(lua_State* L) {
  LuaHost& host = host_of(L);
  const std::string name = check_string(L, 1);
  auto try_files = [&](std::span<const EmbeddedFile> files) -> bool {
    for (const auto& f : files) {
      if (f.module != name) continue;
      const std::string chunk = "@" + std::string(f.path);
      if (luaL_loadbuffer(L, f.source.data(), f.source.size(), chunk.c_str()) != 0) {
        lua_error(L);
      }
      return true;
    }
    return false;
  };
  if (try_files(bootstrap_files()) || try_files(host.extra_files())) return 1;
  lua_pushfstring(L, "\n\tno embedded module '%s'", name.c_str());
  return 1;
}

}  // namespace

LuaHost::LuaHost(const Paths& paths, Fs& fs, Mode mode, std::span<const EmbeddedFile> extra)
    : paths_(paths), fs_(fs), mode_(mode), extra_(extra) {
  L_ = luaL_newstate();
  if (L_ == nullptr) throw std::runtime_error("cannot create Lua state");
  luaL_openlibs(L_);
  lua_pushlightuserdata(L_, const_cast<char*>(&kHostKey));
  lua_pushlightuserdata(L_, this);
  lua_rawset(L_, LUA_REGISTRYINDEX);

  lua_getglobal(L_, "package");
  // Embedded modules only: no filesystem search paths for the bootstrap.
  lua_pushliteral(L_, "");
  lua_setfield(L_, -2, "path");
  lua_pushliteral(L_, "");
  lua_setfield(L_, -2, "cpath");
  lua_getfield(L_, -1, "preload");
  lua_pushcfunction(L_, guarded<open_host_module>);
  lua_setfield(L_, -2, "repro.host");
  lua_pop(L_, 1);
  lua_getfield(L_, -1, "loaders");
  // Insert our searcher right after the preload searcher.
  const int n = static_cast<int>(lua_objlen(L_, -1));
  for (int i = n; i >= 2; --i) {
    lua_rawgeti(L_, -1, i);
    lua_rawseti(L_, -2, i + 1);
  }
  lua_pushcfunction(L_, embedded_searcher);
  lua_rawseti(L_, -2, 2);
  lua_pop(L_, 2);
}

LuaHost::~LuaHost() {
  if (L_ != nullptr) lua_close(L_);
}

void LuaHost::log(std::string_view msg) {
  if (log_sink_ == nullptr) return;
  if (!log_sink_->empty() && log_sink_->back() != '\n') log_sink_->push_back('\n');
  log_sink_->append(msg);
}

Result<Value> LuaHost::call(std::string_view module, std::string_view fn, const List& args) {
  lua_State* L = L_;
  const int base = lua_gettop(L);
  luaL_checkstack(L, static_cast<int>(args.size()) + 8, "repro host call");
  lua_getglobal(L, "require");
  lua_pushlstring(L, module.data(), module.size());
  if (lua_pcall(L, 1, 1, 0) != 0) {
    Error e = error_from_lua(L, -1);
    lua_settop(L, base);
    return std::unexpected(e);
  }
  lua_pushlstring(L, fn.data(), fn.size());
  lua_gettable(L, -2);
  if (!lua_isfunction(L, -1)) {
    lua_settop(L, base);
    return fail(ec::kConfig, std::format("bootstrap function {}.{} is missing", module, fn),
                "this is a bug in repro");
  }
  for (const auto& a : args) push_value(L, a);
  if (lua_pcall(L, static_cast<int>(args.size()), 1, 0) != 0) {
    Error e = error_from_lua(L, -1);
    lua_settop(L, base);
    return std::unexpected(e);
  }
  auto v = to_value(L, -1);
  lua_settop(L, base);
  return v;
}

}  // namespace repro
