// Embeds src/lua/repd/*.lua. #embed is C23; GCC >= 15 and Clang >= 19 accept
// it in C++ as an extension, so the pedantic diagnostic is silenced locally.
#include "eval/embedded.hpp"

#include <array>

namespace repd {
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
#embed "../lua/repd/init.lua"
};
constexpr char kErr[] = {
#embed "../lua/repd/err.lua"
};
constexpr char kUtil[] = {
#embed "../lua/repd/util.lua"
};
constexpr char kCanon[] = {
#embed "../lua/repd/canon.lua"
};
constexpr char kSandbox[] = {
#embed "../lua/repd/sandbox.lua"
};
constexpr char kBuilders[] = {
#embed "../lua/repd/builders.lua"
};
constexpr char kParams[] = {
#embed "../lua/repd/params.lua"
};
constexpr char kMerge[] = {
#embed "../lua/repd/merge.lua"
};
constexpr char kLock[] = {
#embed "../lua/repd/lock.lua"
};
constexpr char kBackend[] = {
#embed "../lua/repd/backend.lua"
};

#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif

constexpr std::string_view view(const char* data, std::size_t n) { return {data, n}; }

const std::array kFiles = {
    EmbeddedFile{"repd", "repd/init.lua", view(kInit, sizeof kInit)},
    EmbeddedFile{"repd.err", "repd/err.lua", view(kErr, sizeof kErr)},
    EmbeddedFile{"repd.util", "repd/util.lua", view(kUtil, sizeof kUtil)},
    EmbeddedFile{"repd.canon", "repd/canon.lua", view(kCanon, sizeof kCanon)},
    EmbeddedFile{"repd.sandbox", "repd/sandbox.lua", view(kSandbox, sizeof kSandbox)},
    EmbeddedFile{"repd.builders", "repd/builders.lua", view(kBuilders, sizeof kBuilders)},
    EmbeddedFile{"repd.params", "repd/params.lua", view(kParams, sizeof kParams)},
    EmbeddedFile{"repd.merge", "repd/merge.lua", view(kMerge, sizeof kMerge)},
    EmbeddedFile{"repd.lock", "repd/lock.lua", view(kLock, sizeof kLock)},
    EmbeddedFile{"repd.backend", "repd/backend.lua", view(kBackend, sizeof kBackend)},
};

}  // namespace

std::span<const EmbeddedFile> bootstrap_files() { return kFiles; }

}  // namespace repd
