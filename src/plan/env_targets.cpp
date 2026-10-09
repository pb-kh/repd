#include "plan/env_targets.hpp"

#include <pwd.h>

#include <algorithm>
#include <format>

#include "plan/scope.hpp"
#include "platform/users.hpp"
#include "util/strings.hpp"

namespace repro {

std::string login_shell(const Paths& paths, const Fs& fs) {
  if (auto e = find_passwd_entry(fs, paths.physical("/etc/passwd"), paths.user().name)) return e->shell;
  // With a sysroot (tests) the sandbox's passwd is authoritative; never consult the host.
  if (paths.sysroot().empty()) {
    if (const passwd* pw = ::getpwnam(paths.user().name.c_str()); pw != nullptr && pw->pw_shell) {
      return pw->pw_shell;
    }
  }
  return "/bin/sh";
}

Result<EnvTargets> resolve_env_targets(const std::string& option, const Paths& paths, const Fs& fs) {
  EnvTargets t;
  const std::string shell = basename(login_shell(paths, fs));
  if (option.empty() || option == "auto") {
    const bool systemd = fs.stat(paths.physical("/run/systemd/system")).type == FileType::Dir;
    const bool fish_installed = fs.stat(paths.physical("/etc/fish")).type == FileType::Dir ||
                                fs.stat(paths.physical("/usr/bin/fish")).exists();
    t.user.push_back(shell == "fish" ? "fish" : "sh");
    t.system.push_back("sh");
    if (fish_installed || shell == "fish") t.system.push_back("fish");
    if (systemd) {
      t.user.push_back("environment.d");
      t.system.push_back("environment.d");
    }
  } else {
    for (const auto& name : split(option, ',')) {
      const std::string n(trim(name));
      if (std::ranges::find(kEnvTargetNames, n) == std::end(kEnvTargetNames)) {
        return fail(ec::kConfig, std::format("unknown env target '{}' in rp.option(\"env.targets\")", n),
                    "use \"auto\" or a list of: sh, fish, environment.d");
      }
      if (std::ranges::find(t.user, n) == t.user.end()) t.user.push_back(n);
    }
    t.system = t.user;
  }
  // The sh target needs one line in the profile the login shell actually reads.
  const std::string home = paths.user().home;
  if (shell == "zsh") {
    t.profile = home + "/.zprofile";
  } else if (shell == "bash" && fs.lstat(paths.physical(home + "/.bash_profile")).exists()) {
    t.profile = home + "/.bash_profile";  // bash skips ~/.profile when this exists
  } else {
    t.profile = home + "/.profile";
  }
  return t;
}

std::string sh_env_dir(const Paths& paths) { return paths.xdg_data_home() + "/repro/env.d"; }

std::string env_file_path(const Paths& paths, const std::string& target, Mode scope,
                          const std::string& module) {
  const std::string slug = module_slug(module);
  const bool user = scope == Mode::User;
  if (target == "sh") {
    return user ? sh_env_dir(paths) + "/" + slug + ".sh" : "/etc/profile.d/repro-" + slug + ".sh";
  }
  if (target == "fish") {
    return (user ? paths.xdg_config_home() + "/fish/conf.d" : std::string("/etc/fish/conf.d")) +
           "/repro-" + slug + ".fish";
  }
  return (user ? paths.user_env_dir() : paths.system_env_dir()) + "/90-repro-" + slug + ".conf";
}

namespace {

std::string sh_quote(const std::string& v) { return "'" + replace_all(v, "'", "'\\''") + "'"; }

std::string fish_quote(const std::string& v) {
  return "'" + replace_all(replace_all(v, "\\", "\\\\"), "'", "\\'") + "'";
}

}  // namespace

std::string env_file_content(const std::string& target, const std::string& module,
                             const std::map<std::string, std::string>& vars) {
  std::string out = std::format("# Managed by repro (module {}). Do not edit.\n", module);
  for (const auto& [k, v] : vars) {
    if (target == "sh") out += std::format("export {}={}\n", k, sh_quote(v));
    else if (target == "fish") out += std::format("set -gx {} {}\n", k, fish_quote(v));
    else out += std::format("{}={}\n", k, v);
  }
  return out;
}

std::vector<std::string> sh_profile_block(const Paths& paths) {
  std::string dir = sh_env_dir(paths);
  if (paths.under_home(dir)) dir = "$HOME" + dir.substr(paths.user().home.size());
  return {
      "# Environment declared in repro modules (env target: sh).",
      std::format("for f in \"{}\"/*.sh; do [ -r \"$f\" ] && . \"$f\"; done; unset f", dir),
  };
}

}  // namespace repro
