// Small string helpers shared by all components.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace repro {

std::string_view trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep, bool keep_empty = false);
std::string join(std::span<const std::string> parts, std::string_view sep);
std::string replace_all(std::string s, std::string_view from, std::string_view to);
bool starts_with_path(std::string_view path, std::string_view prefix);  // component-wise

// Lexically normalises an absolute path: collapses '//' and '.', resolves '..'.
// Returns "" when '..' would escape the root.
std::string normalize_path(std::string_view path);
std::string dirname(std::string_view path);
std::string basename(std::string_view path);

// Levenshtein distance and closest-match lookup for "did you mean" hints.
std::size_t edit_distance(std::string_view a, std::string_view b);
// Returns the closest candidate within a distance budget scaled to the word length, or "".
std::string closest_match(std::string_view word, std::span<const std::string> candidates);

// Lua-compatible %q-free quoting for human output: wraps in double quotes when needed.
std::string shell_quote(std::string_view s);

}  // namespace repro
