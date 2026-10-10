// Command implementations (contracts/cli.md). Each returns the process exit code.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cli/pipeline.hpp"

namespace repd {

struct SyncArgs {
  bool user = false;
  bool system = false;
  std::vector<std::string> modules;
  std::optional<std::string> profile;
  bool dry_run = false;
  bool yes = false;
  bool adopt = false;
  bool upgrade = false;
  std::vector<std::string> refresh;
  std::vector<std::string> providers;
  std::vector<std::string> reselect;
};

std::string version_string();

int cmd_init(const GlobalOptions& g, const std::optional<std::string>& dir, bool force);
int cmd_sync(const GlobalOptions& g, const SyncArgs& a, const std::vector<std::string>& argv);
int cmd_status(const GlobalOptions& g, Mode mode, const std::optional<std::string>& profile);
int cmd_journal(const GlobalOptions& g, Mode mode, int last, std::optional<std::int64_t> run);
int cmd_list(const GlobalOptions& g, const std::string& what, const std::optional<std::string>& profile);
int cmd_show(const GlobalOptions& g, const std::string& module, const std::optional<std::string>& profile);
int cmd_plugins_list(const GlobalOptions& g);

// Prints the error and returns its exit code.
int report(const Error& e);

}  // namespace repd
