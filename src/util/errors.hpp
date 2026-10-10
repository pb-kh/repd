// Structured errors (contracts/cli.md "Error message contract", FR-024, SC-007).
// Every user-facing failure carries a code, a message naming the offending
// element, and one suggested next action.
#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace repd {

// Error codes from the catalogue in contracts/cli.md.
namespace ec {
inline constexpr std::string_view kUsage = "E_USAGE";
inline constexpr std::string_view kConfig = "E_CONFIG";
inline constexpr std::string_view kName = "E_NAME";
inline constexpr std::string_view kUnknown = "E_UNKNOWN";
inline constexpr std::string_view kCollision = "E_COLLISION";
inline constexpr std::string_view kCycle = "E_CYCLE";
inline constexpr std::string_view kNoProvider = "E_NO_PROVIDER";
inline constexpr std::string_view kAmbiguous = "E_AMBIGUOUS";
inline constexpr std::string_view kProvider = "E_PROVIDER";
inline constexpr std::string_view kParam = "E_PARAM";
inline constexpr std::string_view kConflict = "E_CONFLICT";
inline constexpr std::string_view kDeclarative = "E_DECLARATIVE";
inline constexpr std::string_view kNoBackend = "E_NO_BACKEND";
inline constexpr std::string_view kPluginAbi = "E_PLUGIN_ABI";
inline constexpr std::string_view kPrivilege = "E_PRIVILEGE";
inline constexpr std::string_view kBusy = "E_BUSY";
inline constexpr std::string_view kBlocked = "E_BLOCKED";
inline constexpr std::string_view kStep = "E_STEP";
inline constexpr std::string_view kSchema = "E_SCHEMA";
inline constexpr std::string_view kIo = "E_IO";
inline constexpr std::string_view kInterrupted = "E_INTERRUPTED";
}  // namespace ec

// Process exit codes (contracts/cli.md "Exit codes").
inline constexpr int kExitOk = 0;
inline constexpr int kExitApplyFailed = 1;
inline constexpr int kExitConfig = 2;
inline constexpr int kExitInterrupted = 130;

struct Error {
  std::string code;
  std::string message;
  std::string hint;

  // Apply-time failures (step failed, adoption guard) exit 1, interruption 130,
  // everything detected before mutation (config, plan, usage) exits 2.
  int exit_code() const;
};

template <class T>
using Result = std::expected<T, Error>;
using Status = std::expected<void, Error>;

inline std::unexpected<Error> fail(std::string_view code, std::string message,
                                   std::string hint = {}) {
  return std::unexpected(Error{std::string(code), std::move(message), std::move(hint)});
}

// Renders `error[E_CODE]: message\n  hint: ...\n`.
std::string format_error(const Error& e, bool color = false);
void print_error(const Error& e);

// Turns a "did you mean" candidate into hint text, or "" when there is none.
std::string did_you_mean(std::string_view candidate);

}  // namespace repd
