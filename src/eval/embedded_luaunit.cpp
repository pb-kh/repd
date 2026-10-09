// Vendored luaunit (test binary only, research R18).
#include <array>

#include "eval/embedded.hpp"

namespace repro {
namespace {

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wc++26-extensions"
#endif
constexpr char kLuaunit[] = {
#embed "../lua/vendor/luaunit.lua"
};
#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif

const std::array kFiles = {
    EmbeddedFile{"luaunit", "vendor/luaunit.lua", std::string_view(kLuaunit, sizeof kLuaunit)},
};

}  // namespace

std::span<const EmbeddedFile> luaunit_files() { return kFiles; }

}  // namespace repro
