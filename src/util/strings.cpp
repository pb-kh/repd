#include "util/strings.hpp"

#include <algorithm>
#include <cctype>

namespace repro {

std::string_view trim(std::string_view s) {
  const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

std::vector<std::string> split(std::string_view s, char sep, bool keep_empty) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const std::size_t pos = s.find(sep, start);
    const std::size_t end = pos == std::string_view::npos ? s.size() : pos;
    if (keep_empty || end > start) out.emplace_back(s.substr(start, end - start));
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return out;
}

std::string join(std::span<const std::string> parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += sep;
    out += parts[i];
  }
  return out;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
  if (from.empty()) return s;
  std::size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

bool starts_with_path(std::string_view path, std::string_view prefix) {
  while (prefix.size() > 1 && prefix.back() == '/') prefix.remove_suffix(1);
  if (!path.starts_with(prefix)) return false;
  return path.size() == prefix.size() || path[prefix.size()] == '/' || prefix == "/";
}

std::string normalize_path(std::string_view path) {
  std::vector<std::string> parts;
  for (auto& part : split(path, '/')) {
    if (part == ".") continue;
    if (part == "..") {
      if (parts.empty()) return {};
      parts.pop_back();
      continue;
    }
    parts.push_back(std::move(part));
  }
  return "/" + join(parts, "/");
}

std::string dirname(std::string_view path) {
  const std::size_t pos = path.rfind('/');
  if (pos == std::string_view::npos) return ".";
  if (pos == 0) return "/";
  return std::string(path.substr(0, pos));
}

std::string basename(std::string_view path) {
  const std::size_t pos = path.rfind('/');
  return std::string(pos == std::string_view::npos ? path : path.substr(pos + 1));
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
  std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
    }
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

std::string closest_match(std::string_view word, std::span<const std::string> candidates) {
  const std::size_t budget = std::max<std::size_t>(2, word.size() / 3);
  std::string best;
  std::size_t best_dist = budget + 1;
  for (const auto& c : candidates) {
    std::size_t d = edit_distance(word, c);
    // Treat a shared suffix/prefix component (e.g. "vim" vs "dev/ide/vim") as close.
    if (d > budget && (c.ends_with("/" + std::string(word)) || word.ends_with("/" + c))) {
      d = budget;
    }
    if (d < best_dist || (d == best_dist && c < best)) {
      best = c;
      best_dist = d;
    }
  }
  return best_dist <= budget ? best : std::string{};
}

std::string shell_quote(std::string_view s) {
  const bool plain = !s.empty() && std::ranges::all_of(s, [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || std::string_view("/._-+:=@,~").find(c) != std::string_view::npos;
  });
  if (plain) return std::string(s);
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  return out + "'";
}

}  // namespace repro
