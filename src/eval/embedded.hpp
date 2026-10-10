// Lua sources compiled into the binary with #embed (research R4).
#pragma once

#include <span>
#include <string_view>

namespace repd {

struct EmbeddedFile {
  std::string_view module;  // require() name, e.g. "repd.canon"
  std::string_view path;    // chunk name, e.g. "repd/canon.lua"
  std::string_view source;
};

std::span<const EmbeddedFile> bootstrap_files();

}  // namespace repd
