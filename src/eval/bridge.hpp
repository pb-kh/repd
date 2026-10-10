// C++ <-> Lua data bridge (research R9): converts evaluated Lua tables into
// plain Value trees and back. Functions never cross; the bootstrap replaces
// them with source-position ids before handing data to C++.
#pragma once

#include <lua.hpp>

#include "util/errors.hpp"
#include "util/value.hpp"

namespace repd {

void push_value(lua_State* L, const Value& v);
Result<Value> to_value(lua_State* L, int index);

// Maps a Lua error object (structured repd.err table or string) to an Error.
Error error_from_lua(lua_State* L, int index);

}  // namespace repd
