// Embeds src/lua/repro/*.lua. #embed is C23; GCC >= 15 and Clang >= 19 accept
// it in C++ as an extension, so the pedantic diagnostic is silenced locally.
#include "eval/embedded.hpp"

#include <array>

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

constexpr char kInit[] = {
#embed "../lua/repro/init.lua"
};
constexpr char kErr[] = {
#embed "../lua/repro/err.lua"
};
constexpr char kUtil[] = {
#embed "../lua/repro/util.lua"
};
constexpr char kCanon[] = {
#embed "../lua/repro/canon.lua"
};
constexpr char kSandbox[] = {
#embed "../lua/repro/sandbox.lua"
};
constexpr char kBuilders[] = {
#embed "../lua/repro/builders.lua"
};
constexpr char kParams[] = {
#embed "../lua/repro/params.lua"
};
constexpr char kMerge[] = {
#embed "../lua/repro/merge.lua"
};
constexpr char kLock[] = {
#embed "../lua/repro/lock.lua"
};
constexpr char kBackend[] = {
#embed "../lua/repro/backend.lua"
};

#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif

constexpr std::string_view view(const char* data, std::size_t n) { return {data, n}; }

const std::array kFiles = {
    EmbeddedFile{"repro", "repro/init.lua", view(kInit, sizeof kInit)},
    EmbeddedFile{"repro.err", "repro/err.lua", view(kErr, sizeof kErr)},
    EmbeddedFile{"repro.util", "repro/util.lua", view(kUtil, sizeof kUtil)},
    EmbeddedFile{"repro.canon", "repro/canon.lua", view(kCanon, sizeof kCanon)},
    EmbeddedFile{"repro.sandbox", "repro/sandbox.lua", view(kSandbox, sizeof kSandbox)},
    EmbeddedFile{"repro.builders", "repro/builders.lua", view(kBuilders, sizeof kBuilders)},
    EmbeddedFile{"repro.params", "repro/params.lua", view(kParams, sizeof kParams)},
    EmbeddedFile{"repro.merge", "repro/merge.lua", view(kMerge, sizeof kMerge)},
    EmbeddedFile{"repro.lock", "repro/lock.lua", view(kLock, sizeof kLock)},
    EmbeddedFile{"repro.backend", "repro/backend.lua", view(kBackend, sizeof kBackend)},
};

}  // namespace

std::span<const EmbeddedFile> bootstrap_files() { return kFiles; }

}  // namespace repro
