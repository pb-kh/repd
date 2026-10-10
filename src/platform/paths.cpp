#include "platform/paths.hpp"

#include <unistd.h>

#include <format>

#include "util/strings.hpp"

namespace repd {

std::string_view to_string(Mode m) { return m == Mode::User ? "user" : "system"; }
Mode other(Mode m) { return m == Mode::User ? Mode::System : Mode::User; }

namespace {

std::string absolute(std::string_view p) {
  if (p.starts_with('/')) return normalize_path(p);
  char buf[4096];
  const std::string cwd = ::getcwd(buf, sizeof buf) != nullptr ? buf : "/";
  return normalize_path(cwd + "/" + std::string(p));
}

}  // namespace

bool Paths::test_hooks() const {
#if REPD_TEST_HOOKS
  return true;
#else
  return false;
#endif
}

Result<Paths> Paths::detect(const Env& env, const PathsOptions& opts) {
  Paths p;
  p.is_root_ = ::geteuid() == 0;
#if REPD_TEST_HOOKS
  if (auto root = env.get("REPD_SYSROOT")) p.sysroot_ = absolute(*root);
  if (env.get("REPD_TEST_ASSUME_ROOT") == "1") p.is_root_ = true;
#endif
  auto user = resolve_target_user(env, p.is_root_);
  if (!user) return std::unexpected(user.error());
  p.user_ = *user;
  // Under sudo the environment belongs to root; use the target user's defaults.
  const bool use_xdg = !p.user_.via_sudo;
  const auto xdg_config = use_xdg ? env.get("XDG_CONFIG_HOME") : std::nullopt;
  const auto xdg_state = use_xdg ? env.get("XDG_STATE_HOME") : std::nullopt;
  const auto xdg_data = use_xdg ? env.get("XDG_DATA_HOME") : std::nullopt;
  p.xdg_data_ = xdg_data ? absolute(*xdg_data) : p.user_.home + "/.local/share";
  p.xdg_config_ = xdg_config ? absolute(*xdg_config) : p.user_.home + "/.config";
  p.xdg_state_ = xdg_state ? absolute(*xdg_state) : p.user_.home + "/.local/state";
  if (opts.config_override) p.config_root_ = absolute(p.expand(*opts.config_override));
  else if (auto c = env.get("REPD_CONFIG"))
    p.config_root_ = absolute(p.expand(*c));
  else
    p.config_root_ = p.xdg_config_ + "/repd";
  return p;
}

Paths Paths::make(TargetUser user, std::string config_root, std::string sysroot, bool is_root) {
  Paths p;
  p.user_ = std::move(user);
  p.config_root_ = std::move(config_root);
  p.sysroot_ = std::move(sysroot);
  p.is_root_ = is_root;
  p.xdg_config_ = p.user_.home + "/.config";
  p.xdg_state_ = p.user_.home + "/.local/state";
  p.xdg_data_ = p.user_.home + "/.local/share";
  return p;
}

std::string Paths::expand(std::string_view path) const {
  if (path == "~") return user_.home;
  if (path.starts_with("~/")) return user_.home + std::string(path.substr(1));
  return std::string(path);
}

bool Paths::under_home(std::string_view logical) const {
  return starts_with_path(logical, user_.home);
}

std::string Paths::display(std::string_view logical) const {
  if (under_home(logical)) return "~" + std::string(logical.substr(user_.home.size()));
  return std::string(logical);
}

std::string Paths::physical(std::string_view logical) const {
  const std::string p = expand(logical);
  if (sysroot_.empty() || under_home(p) || starts_with_path(p, sysroot_)) return p;
  return sysroot_ + p;
}

std::string Paths::state_dir(Mode m) const {
  if (m == Mode::User) return xdg_state_ + "/repd";
  return physical("/var/lib/repd");
}

std::string Paths::user_env_dir() const { return xdg_config_ + "/environment.d"; }

std::vector<std::pair<std::string, std::string>> Paths::plugin_dirs(Mode m) const {
  // The system directory is a build-time setting (-Dplugin_dir, REPD_PLUGIN_DIR).
  std::vector<std::pair<std::string, std::string>> dirs = {{physical(REPD_PLUGIN_DIR), "system"}};
  if (m == Mode::User) dirs.emplace_back(user_.home + "/.local/lib/repd", "user");
  return dirs;
}

}  // namespace repd
