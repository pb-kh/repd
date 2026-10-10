// Numbered schema migrations (data-model.md §3.1, research R5).
#pragma once

#include <span>
#include <string_view>

namespace repd {

// migrations()[i] upgrades schema version i to i + 1.
std::span<const std::string_view> migrations();

// For a read-only connection to a store at an older schema, which cannot be
// migrated: TEMP views that present it with the current schema's names ("" if
// that version needs none).
std::string_view read_only_compat(int version);

}  // namespace repd
