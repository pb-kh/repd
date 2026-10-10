// Interface -> provider resolution (FR-006, research R16, data-model.md
// "ResolvedProvider"): cli > profile > pin > lock > only-option > prompt,
// otherwise E_AMBIGUOUS; no candidates -> E_NO_PROVIDER.
#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "eval/declaration.hpp"
#include "util/errors.hpp"

namespace repd {

struct LockEntry {
  std::string provider;
  std::string source;
  std::string decided_at;
};

struct ResolvedProvider {
  std::string interface;
  std::string provider;
  std::string source;    // cli|profile|pin|lock|only-option|prompt
  bool persist = false;  // recorded in repd.lock.lua (cli, prompt, only-option)
};

using PromptFn = std::function<Result<std::string>(
    const std::string& iface, const std::vector<const ModuleMeta*>& candidates,
    const std::vector<std::string>& requirers)>;

struct ResolverInputs {
  std::map<std::string, std::string> cli;      // --provider I=M
  std::map<std::string, std::string> profile;  // profile.providers
  std::map<std::string, std::string> pins;     // rp.pin
  std::map<std::string, LockEntry> lock;       // repd.lock.lua
  std::set<std::string> reselect;              // --reselect I
  bool interactive = false;                    // TTY and no --yes
  PromptFn prompt;
};

class Resolver {
 public:
  Resolver(const std::map<std::string, ModuleMeta>& modules, ResolverInputs in);

  Result<ResolvedProvider> resolve(const std::string& iface,
                                   const std::vector<std::string>& requirers);
  // Records a provider chosen by being selected directly (FR-I-5).
  void choose(const std::string& iface, const std::string& provider, std::string source);
  bool has_cli_override(const std::string& iface) const { return in_.cli.contains(iface); }

  // Providers of `iface`, ordered by (priority, name); the first is the default.
  std::vector<const ModuleMeta*> candidates(const std::string& iface) const;
  const std::map<std::string, ResolvedProvider>& resolved() const { return resolved_; }
  const std::vector<std::string>& warnings() const { return warnings_; }

 private:
  Result<std::string> check_override(const std::string& iface, const std::string& provider,
                                     std::string_view origin) const;

  const std::map<std::string, ModuleMeta>& modules_;
  ResolverInputs in_;
  std::map<std::string, ResolvedProvider> resolved_;
  std::vector<std::string> warnings_;
};

}  // namespace repd
