// Plain data tree exchanged between the Lua evaluation layer and C++
// (data-model.md: "handed to C++ as a plain data tree"). Mirrors what a
// canonical Lua value can hold: nil, boolean, number, string, list, map.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace repd {

struct Value;
using List = std::vector<Value>;
using Map = std::map<std::string, Value>;

struct Value {
  std::variant<std::monostate, bool, double, std::string, List, Map> v;

  Value() = default;
  Value(bool b) : v(b) {}                     // NOLINT(google-explicit-constructor)
  Value(double d) : v(d) {}                   // NOLINT
  Value(int i) : v(static_cast<double>(i)) {} // NOLINT
  Value(const char* s) : v(std::string(s)) {} // NOLINT
  Value(std::string s) : v(std::move(s)) {}   // NOLINT
  Value(List l) : v(std::move(l)) {}          // NOLINT
  Value(Map m) : v(std::move(m)) {}           // NOLINT

  bool is_nil() const { return std::holds_alternative<std::monostate>(v); }
  bool is_bool() const { return std::holds_alternative<bool>(v); }
  bool is_number() const { return std::holds_alternative<double>(v); }
  bool is_string() const { return std::holds_alternative<std::string>(v); }
  // An empty Lua table is both an empty list and an empty map.
  bool is_list() const {
    return std::holds_alternative<List>(v) || (is_map() && std::get<Map>(v).empty());
  }
  bool is_map() const {
    return std::holds_alternative<Map>(v) ||
           (std::holds_alternative<List>(v) && std::get<List>(v).empty());
  }

  bool as_bool(bool fallback = false) const { return is_bool() ? std::get<bool>(v) : fallback; }
  double as_number(double fallback = 0) const {
    return is_number() ? std::get<double>(v) : fallback;
  }
  long long as_int(long long fallback = 0) const {
    return is_number() ? static_cast<long long>(std::get<double>(v)) : fallback;
  }
  const std::string& as_string() const;
  const List& as_list() const;
  const Map& as_map() const;

  // Map lookup; returns a nil Value when absent or not a map.
  const Value& operator[](const std::string& key) const;
  std::optional<std::string> opt_string(const std::string& key) const;
  std::string string_or(const std::string& key, std::string fallback) const;
  std::vector<std::string> string_list(const std::string& key) const;
  std::map<std::string, std::string> string_map(const std::string& key) const;

  bool operator==(const Value& other) const = default;
};

}  // namespace repd
