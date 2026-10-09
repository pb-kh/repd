#include "util/value.hpp"

namespace repro {
namespace {
const std::string kEmptyString;
const List kEmptyList;
const Map kEmptyMap;
const Value kNil;
}  // namespace

const std::string& Value::as_string() const {
  return is_string() ? std::get<std::string>(v) : kEmptyString;
}

const List& Value::as_list() const {
  return std::holds_alternative<List>(v) ? std::get<List>(v) : kEmptyList;
}

const Map& Value::as_map() const {
  return std::holds_alternative<Map>(v) ? std::get<Map>(v) : kEmptyMap;
}

const Value& Value::operator[](const std::string& key) const {
  const auto& m = as_map();
  auto it = m.find(key);
  return it == m.end() ? kNil : it->second;
}

std::optional<std::string> Value::opt_string(const std::string& key) const {
  const Value& x = (*this)[key];
  if (!x.is_string()) return std::nullopt;
  return x.as_string();
}

std::string Value::string_or(const std::string& key, std::string fallback) const {
  const Value& x = (*this)[key];
  return x.is_string() ? x.as_string() : std::move(fallback);
}

std::vector<std::string> Value::string_list(const std::string& key) const {
  std::vector<std::string> out;
  for (const auto& item : (*this)[key].as_list()) {
    if (item.is_string()) out.push_back(item.as_string());
  }
  return out;
}

std::map<std::string, std::string> Value::string_map(const std::string& key) const {
  std::map<std::string, std::string> out;
  for (const auto& [k, item] : (*this)[key].as_map()) {
    if (item.is_string()) out.emplace(k, item.as_string());
  }
  return out;
}

}  // namespace repro
