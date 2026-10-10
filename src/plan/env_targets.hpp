// Where `env` / `system_env` are written (research R13, revised): one file per
// module per target, in each consumer's own drop-in directory, so no init
// system is assumed.
//   sh            ~/.local/share/repd/env.d/<module>.sh (+ a managed block in the
//                 login shell's profile that sources them); /etc/profile.d/repd-<module>.sh
//   fish          ~/.config/fish/conf.d/repd-<module>.fish; /etc/fish/conf.d/repd-<module>.fish
//   environment.d ~/.config/environment.d/90-repd-<module>.conf; /etc/environment.d/… (systemd)
// rp.option("env.targets", "auto" | { ... }); auto = the target user's login
// shell (+ fish system-wide when fish is installed) + environment.d when
// systemd is the running init.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "platform/fs.hpp"
#include "platform/paths.hpp"
#include "util/errors.hpp"

namespace repd {

inline constexpr const char* kEnvTargetNames[] = {"sh", "fish", "environment.d"};

struct EnvTargets {
  std::vector<std::string> user;    // for `env`
  std::vector<std::string> system;  // for `system_env`
  std::string profile;              // logical rc file sourcing the sh env.d (user, sh target)
};

// option: "auto" or a comma-separated list (as stored by rp.option).
Result<EnvTargets> resolve_env_targets(const std::string& option, const Paths& paths, const Fs& fs);
std::string login_shell(const Paths& paths, const Fs& fs);

// Logical destination of a module's env file for one target and scope.
std::string env_file_path(const Paths& paths, const std::string& target, Mode scope,
                          const std::string& module);
// File content for one target.
std::string env_file_content(const std::string& target, const std::string& module,
                             const std::map<std::string, std::string>& vars);
// Lines of the managed block added to the login profile for the sh target.
std::vector<std::string> sh_profile_block(const Paths& paths);
std::string sh_env_dir(const Paths& paths);  // logical

}  // namespace repd
