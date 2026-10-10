#include "eval/bridge.hpp"

#include <cmath>
#include <format>
#include <set>

namespace repd {

void push_value(lua_State* L, const Value& v) {
  luaL_checkstack(L, 4, "repd bridge");
  if (v.is_nil()) {
    lua_pushnil(L);
  } else if (v.is_bool()) {
    lua_pushboolean(L, v.as_bool() ? 1 : 0);
  } else if (v.is_number()) {
    lua_pushnumber(L, v.as_number());
  } else if (v.is_string()) {
    const auto& s = v.as_string();
    lua_pushlstring(L, s.data(), s.size());
  } else if (std::holds_alternative<List>(v.v)) {
    const auto& l = v.as_list();
    lua_createtable(L, static_cast<int>(l.size()), 0);
    for (std::size_t i = 0; i < l.size(); ++i) {
      push_value(L, l[i]);
      lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
  } else {
    const auto& m = v.as_map();
    lua_createtable(L, 0, static_cast<int>(m.size()));
    for (const auto& [k, x] : m) {
      lua_pushlstring(L, k.data(), k.size());
      push_value(L, x);
      lua_rawset(L, -3);
    }
  }
}

namespace {

Result<Value> convert(lua_State* L, int index, std::set<const void*>& active, int depth) {
  if (depth > 64) return fail(ec::kConfig, "data nested too deeply", "simplify the declaration");
  switch (lua_type(L, index)) {
    case LUA_TNIL:
      return Value{};
    case LUA_TBOOLEAN:
      return Value{lua_toboolean(L, index) != 0};
    case LUA_TNUMBER:
      return Value{static_cast<double>(lua_tonumber(L, index))};
    case LUA_TSTRING: {
      std::size_t n = 0;
      const char* s = lua_tolstring(L, index, &n);
      return Value{std::string(s, n)};
    }
    case LUA_TTABLE:
      break;
    default:
      return fail(ec::kConfig, std::format("cannot use a {} value here", luaL_typename(L, index)),
                  "configuration data must be strings, numbers, booleans or tables");
  }
  const void* ptr = lua_topointer(L, index);
  if (active.contains(ptr)) return fail(ec::kConfig, "data contains a cycle", "");
  active.insert(ptr);
  const int abs = index < 0 ? lua_gettop(L) + index + 1 : index;
  luaL_checkstack(L, 4, "repd bridge");

  // Sequence 1..n -> List; all-string keys -> Map.
  std::size_t count = 0;
  bool all_strings = true;
  bool all_positive_ints = true;
  lua_pushnil(L);
  while (lua_next(L, abs) != 0) {
    ++count;
    if (lua_type(L, -2) == LUA_TSTRING) {
      all_positive_ints = false;
    } else {
      all_strings = false;
      if (lua_type(L, -2) != LUA_TNUMBER) all_positive_ints = false;
      else {
        const double k = lua_tonumber(L, -2);
        if (k < 1 || std::floor(k) != k) all_positive_ints = false;
      }
    }
    lua_pop(L, 1);
  }
  Value out;
  if (count == 0) {
    out = Value{List{}};
  } else if (all_positive_ints && lua_objlen(L, abs) == count) {
    List list;
    list.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
      lua_rawgeti(L, abs, static_cast<int>(i));
      auto item = convert(L, -1, active, depth + 1);
      lua_pop(L, 1);
      if (!item) return item;
      list.push_back(std::move(*item));
    }
    out = Value{std::move(list)};
  } else if (all_strings) {
    Map map;
    lua_pushnil(L);
    while (lua_next(L, abs) != 0) {
      std::size_t n = 0;
      const char* k = lua_tolstring(L, -2, &n);
      auto item = convert(L, -1, active, depth + 1);
      lua_pop(L, 1);
      if (!item) {
        lua_pop(L, 1);
        return item;
      }
      map.emplace(std::string(k, n), std::move(*item));
    }
    out = Value{std::move(map)};
  } else {
    active.erase(ptr);
    return fail(ec::kConfig, "table mixes list items and named keys",
                "use either a list { a, b } or a map { key = value }");
  }
  active.erase(ptr);
  return out;
}

}  // namespace

Result<Value> to_value(lua_State* L, int index) {
  std::set<const void*> active;
  return convert(L, index, active, 0);
}

Error error_from_lua(lua_State* L, int index) {
  if (lua_istable(L, index)) {
    const int abs = index < 0 ? lua_gettop(L) + index + 1 : index;
    auto field = [&](const char* name) {
      lua_getfield(L, abs, name);
      std::string s = lua_isstring(L, -1) != 0 ? lua_tostring(L, -1) : "";
      lua_pop(L, 1);
      return s;
    };
    Error e{field("code"), field("message"), field("hint")};
    if (!e.code.empty()) return e;
  }
  const char* msg = lua_tostring(L, index);
  return Error{std::string(ec::kConfig), msg != nullptr ? msg : "unknown Lua error",
               "fix the Lua error at the reported location"};
}

}  // namespace repd
