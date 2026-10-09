// Numbered schema migrations (data-model.md §3.1, research R5).
#pragma once

#include <span>
#include <string_view>

namespace repro {

// migrations()[i] upgrades schema version i to i + 1.
std::span<const std::string_view> migrations();

}  // namespace repro
