// User/group lookups (research R12). Target-user resolution honours SUDO_USER;
// account checks read passwd/group files through Fs so they work in a sysroot.
#pragma once

#include <sys/types.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "platform/fs.hpp"
#include "util/errors.hpp"

namespace repd {

// Snapshot of the process environment (injectable for tests).
struct Env {
  std::map<std::string, std::string> vars;
  static Env from_process();
  std::optional<std::string> get(const std::string& name) const;
};

struct TargetUser {
  std::string name;
  uid_t uid = 0;
  gid_t gid = 0;
  std::string home;
  bool via_sudo = false;  // resolved from $SUDO_USER
};

// The user whose home/config repd manages: the invoking user, or $SUDO_USER under sudo.
Result<TargetUser> resolve_target_user(const Env& env, bool effective_root);

struct PasswdEntry {
  std::string name;
  uid_t uid = 0;
  gid_t gid = 0;
  std::string home;
  std::string shell;
};

// Parse /etc/passwd-format and /etc/group-format files (physical paths).
std::optional<PasswdEntry> find_passwd_entry(const Fs& fs, const std::string& passwd_path,
                                             const std::string& name);
// Supplementary groups listing `user` as a member.
std::vector<std::string> member_groups(const Fs& fs, const std::string& group_path,
                                       const std::string& user);
bool group_exists(const Fs& fs, const std::string& group_path, const std::string& group);

}  // namespace repd
