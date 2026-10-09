#include "platform/users.hpp"

#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <format>

#include "util/strings.hpp"

extern char** environ;

namespace repro {

Env Env::from_process() {
  Env env;
  for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
    const std::string_view kv = *e;
    const std::size_t eq = kv.find('=');
    if (eq == std::string_view::npos) continue;
    env.vars.emplace(std::string(kv.substr(0, eq)), std::string(kv.substr(eq + 1)));
  }
  return env;
}

std::optional<std::string> Env::get(const std::string& name) const {
  auto it = vars.find(name);
  if (it == vars.end() || it->second.empty()) return std::nullopt;
  return it->second;
}

Result<TargetUser> resolve_target_user(const Env& env, bool effective_root) {
  TargetUser user;
  const auto sudo_user = env.get("SUDO_USER");
  if (effective_root && sudo_user && *sudo_user != "root") {
    const passwd* pw = ::getpwnam(sudo_user->c_str());
    if (pw == nullptr) {
      return fail(ec::kConfig, std::format("SUDO_USER '{}' does not exist", *sudo_user),
                  "run repro through sudo from a regular account");
    }
    user.name = pw->pw_name;
    user.uid = pw->pw_uid;
    user.gid = pw->pw_gid;
    user.home = pw->pw_dir;
    user.via_sudo = true;
    return user;
  }
  const passwd* pw = ::getpwuid(::geteuid());
  user.uid = ::geteuid();
  user.gid = ::getegid();
  user.name = pw != nullptr ? pw->pw_name : env.get("USER").value_or("unknown");
  if (auto home = env.get("HOME")) user.home = *home;
  else if (pw != nullptr) user.home = pw->pw_dir;
  else return fail(ec::kConfig, "cannot determine the home directory", "set $HOME");
  while (user.home.size() > 1 && user.home.back() == '/') user.home.pop_back();
  return user;
}

namespace {

std::vector<std::vector<std::string>> read_colon_file(const Fs& fs, const std::string& path) {
  std::vector<std::vector<std::string>> rows;
  auto text = fs.read(path);
  if (!text) return rows;
  for (const auto& line : split(*text, '\n')) {
    if (line.empty() || line.front() == '#') continue;
    rows.push_back(split(line, ':', /*keep_empty=*/true));
  }
  return rows;
}

}  // namespace

std::optional<PasswdEntry> find_passwd_entry(const Fs& fs, const std::string& passwd_path,
                                             const std::string& name) {
  for (const auto& f : read_colon_file(fs, passwd_path)) {
    if (f.size() < 7 || f[0] != name) continue;
    PasswdEntry e;
    e.name = f[0];
    e.uid = static_cast<uid_t>(std::strtoul(f[2].c_str(), nullptr, 10));
    e.gid = static_cast<gid_t>(std::strtoul(f[3].c_str(), nullptr, 10));
    e.home = f[5];
    e.shell = f[6];
    return e;
  }
  return std::nullopt;
}

std::vector<std::string> member_groups(const Fs& fs, const std::string& group_path,
                                       const std::string& user) {
  std::vector<std::string> out;
  for (const auto& f : read_colon_file(fs, group_path)) {
    if (f.size() < 4) continue;
    const auto members = split(f[3], ',');
    if (std::ranges::find(members, user) != members.end()) out.push_back(f[0]);
  }
  std::ranges::sort(out);
  return out;
}

bool group_exists(const Fs& fs, const std::string& group_path, const std::string& group) {
  return std::ranges::any_of(read_colon_file(fs, group_path),
                             [&](const auto& f) { return !f.empty() && f[0] == group; });
}

}  // namespace repro
