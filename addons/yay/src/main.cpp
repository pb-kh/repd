// repro-yay: packages backend for yay, i.e. repo and AUR packages (contracts/plugin-abi.md).
// yay refuses to run as root and calls sudo itself for the pacman steps, so the
// backend has user privilege (its packages belong to the user pass) and runs yay
// on the terminal (exec_tty): the user sees the build and answers sudo's
// password prompt. Credentials are checked up front, so a run without a
// terminal fails with a clear error instead of hanging in sudo. AUR build
// scripts are reviewed in yay's diff menu unless yay.review is "skip".
#include <repro/plugin.h>
#include <unistd.h>

#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

const repro_host* g_host = nullptr;
const repro_host_api* g_api = nullptr;
bool g_authenticated = false;  // sudo credentials were checked in this run

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

// Built from string_views: a braced std::vector<std::string> temporary here makes
// GCC 16 report a bogus -Wfree-nonheap-object once inlined.
Output run(std::initializer_list<std::string_view> args) {
  return run(std::vector<std::string>(args.begin(), args.end()));
}

int run_tty(const std::vector<std::string>& argv) { return g_api->exec_tty(g_host, c_argv(argv).data()); }

std::vector<std::string> with_names(std::vector<std::string> argv, const char* const* names, size_t n) {
  argv.emplace_back("--");
  for (size_t i = 0; i < n; ++i) argv.emplace_back(names[i]);
  return argv;
}

repro_status error(const std::string& msg) {
  g_api->set_error(g_host, msg.c_str());
  return REPRO_ERR;
}

// The privilege tool yay is configured with (`sudobin` in `yay -Pg`).
std::string sudo_bin() {
  const Output o = run({"yay", "-Pg"});
  const std::string key = "\"sudobin\": \"";
  const auto at = o.out.find(key);
  if (o.code != 0 || at == std::string::npos) return "sudo";
  const auto start = at + key.size();
  const std::string bin = o.out.substr(start, o.out.find('"', start) - start);
  return bin.empty() ? "sudo" : bin;
}

// Makes sure yay's sudo calls will not stall: cached credentials (or NOPASSWD)
// are used as they are; otherwise sudo asks for the password on the terminal.
repro_status authenticate() {
  if (g_authenticated) return REPRO_OK;
  const std::string bin = sudo_bin();
  const std::string_view base = std::string_view(bin).substr(bin.rfind('/') + 1);
  if (base != "sudo") {  // doas & co.: yay asks through them on the terminal itself
    g_authenticated = true;
    return REPRO_OK;
  }
  if (run({bin, "-n", "true"}).code != 0) {
    if (::isatty(STDIN_FILENO) != 1) {
      return error(
          "yay needs sudo for pacman, but sudo wants a password and repro is not running in a terminal\n"
          "run `repro sync -u` in a terminal, or authenticate first with `sudo -v`");
    }
    if (run_tty({bin, "-v", "-p", "[repro] sudo password for %u (yay installs packages): "}) != 0) {
      return error("sudo authentication failed\nre-run and enter your password, or check your sudoers entry");
    }
  }
  g_authenticated = true;
  return REPRO_OK;
}

repro_status refuse_root() {
  if (::geteuid() != 0) return REPRO_OK;
  return error("yay must not run as root\nits packages belong to the user pass: run `repro sync -u` as yourself");
}

int probe(void*) { return ::access("/usr/bin/yay", X_OK) == 0 ? 1 : 0; }

repro_status installed(void*, const char* const* names, size_t n, unsigned char* out_flags) {
  // Same local database for repo and AUR packages. `pacman -T` (deptest) prints
  // the names nothing installed satisfies, honouring provides (yay-bin → yay);
  // `yay -T` would add a status line of its own.
  const Output o = run(with_names({"pacman", "-T"}, names, n));
  if (o.code != 0 && o.code != 127) {
    return error("pacman -T failed (exit " + std::to_string(o.code) + ")" +
                 (o.err.empty() ? "" : ": " + o.err.substr(0, o.err.find('\n'))));
  }
  std::set<std::string> missing;
  std::istringstream lines(o.out);
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty()) missing.insert(line);
  }
  for (size_t i = 0; i < n; ++i) out_flags[i] = missing.count(names[i]) != 0 ? 0 : 1;
  return REPRO_OK;
}

// yay.review = "diff" (default): yay's diff menu is forced on, with any saved
// answer cleared, so it shows each AUR package's PKGBUILD (a diff on updates)
// and asks before building; that needs a terminal. "skip": no menus and no
// confirmations. Either way --sudoloop keeps sudo alive during long builds.
bool review() {
  const char* v = g_api->option(g_host, "yay.review");
  return v == nullptr || std::string_view(v) != "skip";
}

repro_status yay(const char* op, bool needed, const char* const* names, size_t n) {
  if (refuse_root() != REPRO_OK) return REPRO_ERR;
  const bool reviewing = review() && std::string_view(op) != "-R";
  if (reviewing && ::isatty(STDIN_FILENO) != 1) {
    return error(
        "yay.review is \"diff\": yay shows AUR build scripts and asks before building, but repro is not "
        "running in a terminal\n"
        "run `repro sync -u` in a terminal, or skip the review for this run: --option yay.review=skip");
  }
  if (authenticate() != REPRO_OK) return REPRO_ERR;
  std::vector<std::string> argv;
  argv.reserve(12 + n);
  argv.emplace_back("yay");
  argv.emplace_back(op);
  if (needed) argv.emplace_back("--needed");
  if (reviewing) {
    for (const char* flag : {"--diffmenu", "--noanswerdiff", "--answerclean", "None", "--answeredit", "None"}) {
      argv.emplace_back(flag);
    }
  } else {
    for (const char* flag : {"--noconfirm", "--answerclean", "None", "--answerdiff", "None", "--answeredit", "None"}) {
      argv.emplace_back(flag);
    }
  }
  argv.emplace_back("--sudoloop");
  if (n > 0) argv.emplace_back("--");
  for (size_t i = 0; i < n; ++i) argv.emplace_back(names[i]);
  const int code = run_tty(argv);
  if (code == 0) return REPRO_OK;
  return error(std::string("yay ") + op + " failed (exit " + std::to_string(code) +
               ")\nyay's output is shown above; fix the cause and re-run");
}

// Plain -S: refreshing the databases without upgrading would be a partial
// upgrade; `repro sync --upgrade` runs upgrade() first instead.
repro_status install(void*, const char* const* names, size_t n) { return yay("-S", true, names, n); }

// repro sync --upgrade: repo and AUR packages, reviewed like installs.
repro_status upgrade(void*) { return yay("-Syu", false, nullptr, 0); }

repro_status remove(void*, const char* const* names, size_t n) { return yay("-R", false, names, n); }

// repro sync --upgrade MODULE…: without a database refresh, so AUR packages are
// rebuilt when the AUR has a newer version and repo packages only move to the
// version the last full upgrade fetched (never a partial upgrade).
repro_status upgrade_packages(void*, const char* const* names, size_t n) { return yay("-S", true, names, n); }

const repro_packages_vt kVtable = {probe, installed, install, remove, nullptr, upgrade, upgrade_packages, nullptr};

}  // namespace

extern "C" int repro_plugin_abi(void) { return REPRO_PLUGIN_ABI; }

extern "C" int repro_plugin_init(int abi, const repro_host* host, const repro_host_api* api,
                                 repro_registrar* reg, const repro_registrar_api* reg_api) {
  if (abi != REPRO_PLUGIN_ABI) return 1;
  g_host = host;
  g_api = api;
  if (reg_api->declare_option(reg, "yay.review", "diff,skip", "diff") != 0) return 1;
  return reg_api->register_packages(reg, "yay", REPRO_ADDON_VERSION, REPRO_PRIV_USER, &kVtable, nullptr);
}
