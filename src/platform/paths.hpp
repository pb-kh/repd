// Location policy (research R5, R12, R17, R20): config repository, per-mode
// state stores, environment.d targets, plugin search dirs, and the test-only
// sysroot redirection. *Logical* paths are what users declare and what the
// ledger records; *physical* paths are what the Fs touches.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/users.hpp"
#include "util/errors.hpp"

namespace repd {

enum class Mode { User, System };
std::string_view to_string(Mode m);
Mode other(Mode m);

struct PathsOptions {
  std::optional<std::string> config_override;  // --config
};

class Paths {
 public:
  static Result<Paths> detect(const Env& env, const PathsOptions& opts = {});
  // Fully explicit construction for unit tests.
  static Paths make(TargetUser user, std::string config_root, std::string sysroot = {},
                    bool is_root = false);

  const TargetUser& user() const { return user_; }
  const std::string& config_root() const { return config_root_; }
  const std::string& sysroot() const { return sysroot_; }
  bool is_root() const { return is_root_; }
  bool test_hooks() const;

  // `~` / `~/x` → absolute under the target home. Other strings unchanged.
  std::string expand(std::string_view path) const;
  // Absolute paths under the target home → `~/...` for display and step ids.
  std::string display(std::string_view logical) const;
  bool under_home(std::string_view logical) const;
  // Logical → physical: the home is never redirected; everything else gets the sysroot prefix.
  std::string physical(std::string_view logical) const;

  std::string state_dir(Mode m) const;      // physical
  const std::string& xdg_config_home() const { return xdg_config_; }  // logical
  const std::string& xdg_data_home() const { return xdg_data_; }      // logical
  std::string user_env_dir() const;         // logical (~/.config/environment.d)
  std::string system_env_dir() const { return "/etc/environment.d"; }  // logical
  std::vector<std::pair<std::string, std::string>> plugin_dirs(Mode m) const;  // {physical, origin}

 private:
  TargetUser user_;
  std::string config_root_;
  std::string sysroot_;
  std::string xdg_config_;
  std::string xdg_state_;
  std::string xdg_data_;
  bool is_root_ = false;
};

}  // namespace repd
