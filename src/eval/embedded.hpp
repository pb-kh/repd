// Lua sources compiled into the binary with #embed (research R4).
#pragma once

#include <span>
#include <string_view>

namespace repro {

struct EmbeddedFile {
  std::string_view module;  // require() name, e.g. "repro.canon"
  std::string_view path;    // chunk name, e.g. "repro/canon.lua"
  std::string_view source;
};

std::span<const EmbeddedFile> bootstrap_files();

}  // namespace repro
