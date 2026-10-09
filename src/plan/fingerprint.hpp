// Module decision hash (research R10): SHA-256 over the canonical encoding
// (canon.lua) of the fingerprint record.
#pragma once

#include <string>

#include "eval/host.hpp"
#include "util/errors.hpp"
#include "util/value.hpp"

namespace repro {

inline constexpr int kFingerprintSchema = 1;

Result<std::string> fingerprint(LuaHost& host, const Value& record);

}  // namespace repro
