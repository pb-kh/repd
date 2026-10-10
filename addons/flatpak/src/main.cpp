// repd-flatpak: packages backends for flatpak (contracts/plugin-abi.md).
// One backend per installation, since its privilege decides the pass it runs in:
// `flatpak` is the system-wide installation (system pass, as root) and
// `flatpak-user` the per-user one (`--user`, user pass). Entries are application
// IDs (org.mozilla.firefox) or full refs (app/org.mozilla.firefox/x86_64/stable).
// Installs and updates run on the terminal (exec_tty): runtimes are large
// downloads and flatpak's progress is the only sign of life.
#include <repd/plugin.h>
#include <unistd.h>

#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

const repd_host* g_host = nullptr;
const repd_host_api* g_api = nullptr;

struct Installation {
  const char* backend;  // registered name
  const char* flag;     // --system | --user
  bool user;
};

const Installation kSystem = {"flatpak", "--system", false};
const Installation kUser = {"flatpak-user", "--user", true};

const Installation& at(void* ud) { return *static_cast<const Installation*>(ud); }

struct Output {
  int code = 0;
  std::string out;
  std::string err;
};

std::vector<const char*> c_argv(const std::vector<std::string>& argv) {
  std::vector<const char*> c;
  for (const auto& a : argv) c.push_back(a.c_str());
  c.push_back(nullptr);
  return c;
}

Output run(const std::vector<std::string>& argv) {
  char* out = nullptr;
  char* err = nullptr;
  Output o;
  o.code = g_api->exec(g_host, c_argv(argv).data(), &out, &err);
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

repd_status error(const std::string& msg) {
  g_api->set_error(g_host, msg.c_str());
  return REPD_ERR;
}

// The per-user installation lives in the user's home: as root it would be root's.
repd_status refuse_root(const Installation& in) {
  if (!in.user || ::geteuid() != 0) return REPD_OK;
  return error(
      "flatpak-user installs into your home and must not run as root\n"
      "its packages belong to the user pass: run `repd sync -u` as yourself");
}

int probe(void*) { return ::access("/usr/bin/flatpak", X_OK) == 0 ? 1 : 0; }

// "app/org.foo.Bar/x86_64/stable" as `flatpak list` prints it: without the kind.
std::string_view without_kind(std::string_view ref) {
  for (std::string_view kind : {"app/", "runtime/"}) {
    if (ref.starts_with(kind)) return ref.substr(kind.size());
  }
  return ref;
}

repd_status installed(void* ud, const char* const* names, size_t n, unsigned char* out_flags) {
  const Installation& in = at(ud);
  if (refuse_root(in) != REPD_OK) return REPD_ERR;
  // Apps and runtimes, one "<application>\t<ref>" line each.
  const Output o = run({"flatpak", "list", in.flag, "--columns=application,ref"});
  if (o.code != 0) {
    return error("flatpak list " + std::string(in.flag) + " failed (exit " + std::to_string(o.code) + ")" +
                 (o.err.empty() ? "" : ": " + o.err.substr(0, o.err.find('\n'))));
  }
  std::set<std::string, std::less<>> have;
  std::istringstream lines(o.out);
  for (std::string line; std::getline(lines, line);) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    have.insert(line.substr(0, tab));
    have.insert(line.substr(tab + 1));
  }
  for (size_t i = 0; i < n; ++i) out_flags[i] = have.contains(without_kind(names[i])) ? 1 : 0;
  return REPD_OK;
}

repd_status flatpak(const Installation& in, const char* op, const char* const* names, size_t n) {
  if (refuse_root(in) != REPD_OK) return REPD_ERR;
  std::vector<std::string> argv = {"flatpak", op, in.flag, "--noninteractive", "-y"};
  if (n > 0) argv.emplace_back("--");
  for (size_t i = 0; i < n; ++i) argv.emplace_back(names[i]);
  const int code = g_api->exec_tty(g_host, c_argv(argv).data());
  if (code == 0) return REPD_OK;
  std::string hint = "flatpak's output is shown above; fix the cause and re-run";
  if (std::string_view(op) == "install") {
    hint = std::string("flatpak's output is shown above; if no remote has it, add one first: flatpak remote-add ") +
           in.flag +
           " --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo";
  }
  return error(std::string("flatpak ") + op + " " + in.flag + " failed (exit " + std::to_string(code) + ")\n" +
               hint);
}

// Without a remote, flatpak looks the IDs up in every remote of the installation.
repd_status install(void* ud, const char* const* names, size_t n) {
  return flatpak(at(ud), "install", names, n);
}

// repd sync --upgrade: every app and runtime of the installation.
repd_status upgrade(void* ud) {
  return flatpak(at(ud), "update", nullptr, 0);
}

// repd sync --upgrade MODULE…: apps update on their own; each runs on its runtime.
repd_status upgrade_packages(void* ud, const char* const* names, size_t n) {
  return flatpak(at(ud), "update", names, n);
}

repd_status remove(void* ud, const char* const* names, size_t n) {
  return flatpak(at(ud), "uninstall", names, n);
}

// No refresh: flatpak fetches remote metadata on every install and update.
const repd_packages_vt kVtable = {probe,   installed, install,          remove,
                                  nullptr, upgrade,   upgrade_packages, nullptr};

}  // namespace

extern "C" int repd_plugin_abi(void) {
  return REPD_PLUGIN_ABI;
}

extern "C" int repd_plugin_init(int abi, const repd_host* host, const repd_host_api* api,
                                repd_registrar* reg, const repd_registrar_api* reg_api) {
  if (abi != REPD_PLUGIN_ABI) return 1;
  g_host = host;
  g_api = api;
  void* system = const_cast<Installation*>(&kSystem);
  void* user = const_cast<Installation*>(&kUser);
  if (reg_api->register_packages(reg, kSystem.backend, REPD_ADDON_VERSION, REPD_PRIV_SYSTEM,
                                 &kVtable, system) != 0) {
    return 1;
  }
  return reg_api->register_packages(reg, kUser.backend, REPD_ADDON_VERSION, REPD_PRIV_USER,
                                    &kVtable, user);
}
