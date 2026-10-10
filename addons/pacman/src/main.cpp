// repd-pacman: packages backend for pacman (contracts/plugin-abi.md).
// Only the C ABI in <repd/plugin.h> crosses the boundary; commands run through
// the host's exec (argv, never a shell) so output lands in the journal.
#include <repd/plugin.h>
#include <unistd.h>

#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

const repd_host* g_host = nullptr;
const repd_host_api* g_api = nullptr;

struct Output {
  int code = 0;
  std::string out;
  std::string err;
};

Output run(std::vector<std::string> argv) {
  std::vector<const char*> c;
  for (const auto& a : argv) c.push_back(a.c_str());
  c.push_back(nullptr);
  char* out = nullptr;
  char* err = nullptr;
  Output o;
  o.code = g_api->exec(g_host, c.data(), &out, &err);
  if (out != nullptr) {
    o.out = out;
    g_api->free_str(out);
  }
  if (err != nullptr) {
    o.err = err;
    g_api->free_str(err);
  }
  return o;
}

std::vector<std::string> with_names(std::vector<std::string> argv, const char* const* names, size_t n) {
  argv.emplace_back("--");
  for (size_t i = 0; i < n; ++i) argv.emplace_back(names[i]);
  return argv;
}

repd_status check(const Output& o, const char* what) {
  if (o.code == 0) return REPD_OK;
  std::string msg = std::string(what) + " failed (exit " + std::to_string(o.code) + ")";
  if (!o.err.empty()) msg += ": " + o.err.substr(0, o.err.find('\n'));
  g_api->set_error(g_host, msg.c_str());
  return REPD_ERR;
}

int probe(void*) { return ::access("/usr/bin/pacman", X_OK) == 0 ? 1 : 0; }

repd_status installed(void*, const char* const* names, size_t n, unsigned char* out_flags) {
  // `pacman -T -- a b c` (deptest) prints the names nothing installed satisfies,
  // exit 127 if there are any. Unlike -Q it honours provides: `yay` is
  // satisfied by yay-bin.
  const Output o = run(with_names({"pacman", "-T"}, names, n));
  if (o.code != 0 && o.code != 127) return check(o, "pacman -T");
  std::set<std::string> missing;
  std::istringstream lines(o.out);
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty()) missing.insert(line);
  }
  for (size_t i = 0; i < n; ++i) out_flags[i] = missing.count(names[i]) != 0 ? 0 : 1;
  return REPD_OK;
}

repd_status install(void*, const char* const* names, size_t n) {
  return check(run(with_names({"pacman", "-S", "--needed", "--noconfirm"}, names, n)), "pacman -S");
}

repd_status remove(void*, const char* const* names, size_t n) {
  return check(run(with_names({"pacman", "-R", "--noconfirm"}, names, n)), "pacman -R");
}

repd_status refresh(void*) {
  return check(run({"pacman", "-Sy"}), "pacman -Sy");
}

// repd sync --upgrade: a full system upgrade, shown on the terminal (it can be long).
repd_status upgrade(void*) {
  const char* argv[] = {"pacman", "-Syu", "--noconfirm", nullptr};
  const int code = g_api->exec_tty(g_host, argv);
  if (code == 0) return REPD_OK;
  const std::string msg = "pacman -Syu failed (exit " + std::to_string(code) +
                          ")\npacman's output is shown above; fix the cause and re-run";
  g_api->set_error(g_host, msg.c_str());
  return REPD_ERR;
}

// repd sync --upgrade MODULE…: refreshing the databases and upgrading only some
// packages is a partial upgrade, so it needs pacman.partial = "allow". pacman
// still refuses anything that breaks a declared dependency of an installed package.
int can_upgrade_packages(void*) {
  const char* v = g_api->option(g_host, "pacman.partial");
  if (v != nullptr && std::string(v) == "allow") return 1;
  g_api->set_error(
      g_host,
      "pacman.partial is \"refuse\": refreshing the databases to upgrade single packages is a "
      "partial upgrade\n"
      "upgrade everything (repd sync --upgrade, without modules), or allow it: "
      "--option pacman.partial=allow");
  return 0;
}

repd_status upgrade_packages(void*, const char* const* names, size_t n) {
  if (can_upgrade_packages(nullptr) != 1) return REPD_ERR;
  std::vector<std::string> argv = with_names({"pacman", "-Sy", "--needed", "--noconfirm"}, names, n);
  std::vector<const char*> c;
  for (const auto& a : argv) c.push_back(a.c_str());
  c.push_back(nullptr);
  const int code = g_api->exec_tty(g_host, c.data());
  if (code == 0) return REPD_OK;
  const std::string msg = "pacman -Sy failed (exit " + std::to_string(code) +
                          ")\npacman's output is shown above; fix the cause and re-run";
  g_api->set_error(g_host, msg.c_str());
  return REPD_ERR;
}

const repd_packages_vt kVtable = {probe,   installed, install,          remove,
                                  refresh, upgrade,   upgrade_packages, can_upgrade_packages};

}  // namespace

extern "C" int repd_plugin_abi(void) {
  return REPD_PLUGIN_ABI;
}

extern "C" int repd_plugin_init(int abi, const repd_host* host, const repd_host_api* api,
                                repd_registrar* reg, const repd_registrar_api* reg_api) {
  if (abi != REPD_PLUGIN_ABI) return 1;
  g_host = host;
  g_api = api;
  if (reg_api->declare_option(reg, "pacman.partial", "refuse,allow", "refuse") != 0) return 1;
  return reg_api->register_packages(reg, "pacman", REPD_ADDON_VERSION, REPD_PRIV_SYSTEM, &kVtable,
                                    nullptr);
}
