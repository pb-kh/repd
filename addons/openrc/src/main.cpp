// repd-openrc: services backend for OpenRC (contracts/plugin-abi.md).
// enabled = the service is in any runlevel (`rc-update show`); enable adds it to
// `default`, disable removes it from all runlevels. active = `rc-service <svc>
// status` exits 0. User scope maps to OpenRC user services (`--user`, 0.60+) and
// is refused under root: it belongs to the user pass.
#include <repd/plugin.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

const repd_host* g_host = nullptr;
const repd_host_api* g_api = nullptr;

constexpr const char* kRunlevel = "default";

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

std::vector<std::string> tool(const char* name, const char* scope) {
  std::vector<std::string> argv = {name};
  if (std::string_view(scope) == "user") argv.emplace_back("--user");
  return argv;
}

bool is_dir(const char* path) {
  struct stat st{};
  return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool has_rc_service() {
  for (const char* p : {"/usr/bin/rc-service", "/usr/sbin/rc-service", "/sbin/rc-service", "/bin/rc-service"}) {
    if (::access(p, X_OK) == 0) return true;
  }
  return false;
}

bool refuse_user_scope_as_root(const char* scope) {
  if (std::string_view(scope) == "user" && ::geteuid() == 0) {
    g_api->set_error(g_host, "user services belong to the user pass (run: repd sync -u)");
    return true;
  }
  return false;
}

// Service names come from modules shared with systemd hosts: `sshd.service`
// means `sshd`; other systemd unit types have no OpenRC counterpart.
bool service_name(const char* unit, std::string* out) {
  std::string name(unit);
  constexpr std::array<std::string_view, 9> kSystemdOnly = {
      ".socket", ".timer", ".target", ".path", ".mount", ".automount", ".swap", ".slice", ".scope"};
  for (auto suffix : kSystemdOnly) {
    if (name.ends_with(suffix)) {
      g_api->set_error(g_host, (name + " is a systemd unit type that OpenRC has no equivalent for").c_str());
      return false;
    }
  }
  if (name.ends_with(".service")) name.resize(name.size() - std::string_view(".service").size());
  const bool ok = !name.empty() && name.front() != '-' && name.find('/') == std::string::npos;
  if (!ok) {
    g_api->set_error(g_host, ("invalid OpenRC service name: " + std::string(unit)).c_str());
    return false;
  }
  *out = std::move(name);
  return true;
}

// `rc-update show` lines look like `          dbus |      default      `.
bool in_any_runlevel(const std::string& show, const std::string& name) {
  std::istringstream in(show);
  for (std::string line; std::getline(in, line);) {
    const auto bar = line.find('|');
    if (bar == std::string::npos) continue;
    const auto b = line.find_first_not_of(' ');
    const auto e = line.find_last_not_of(' ', bar - 1);
    if (b == std::string::npos || b >= bar || line.substr(b, e - b + 1) != name) continue;
    return line.find_first_not_of(" \t\r", bar + 1) != std::string::npos;
  }
  return false;
}

// Prefer the running init: a host with both OpenRC and systemd installed boots one.
int probe(void*) {
  if (is_dir("/run/openrc")) return 1;
  return has_rc_service() && !is_dir("/run/systemd/system") ? 1 : 0;
}

repd_status state(void*, const char* unit, const char* scope, int* enabled, int* active) {
  if (refuse_user_scope_as_root(scope)) return REPD_ERR;
  std::string name;
  if (!service_name(unit, &name)) return REPD_ERR;
  auto exists = tool("rc-service", scope);
  exists.insert(exists.end(), {"--exists", name});
  if (run(exists).code != 0) {
    *enabled = -1;
    *active = 0;
    return REPD_OK;
  }
  auto show = tool("rc-update", scope);
  show.insert(show.end(), {"--nocolor", "show"});
  const Output s = run(show);
  if (s.code != 0) {
    std::string msg = "rc-update show failed (exit " + std::to_string(s.code) + ")";
    if (!s.err.empty()) msg += ": " + s.err.substr(0, s.err.find('\n'));
    g_api->set_error(g_host, msg.c_str());
    return REPD_ERR;
  }
  *enabled = in_any_runlevel(s.out, name) ? 1 : 0;
  auto status = tool("rc-service", scope);
  status.insert(status.end(), {"--nocolor", name, "status"});
  *active = run(status).code == 0 ? 1 : 0;
  return REPD_OK;
}

repd_status op(const char* verb, const char* unit, const char* scope) {
  if (refuse_user_scope_as_root(scope)) return REPD_ERR;
  std::string name;
  if (!service_name(unit, &name)) return REPD_ERR;
  const std::string v(verb);
  std::vector<std::string> argv;
  if (v == "enable") {
    argv = tool("rc-update", scope);
    argv.insert(argv.end(), {"add", name, kRunlevel});
  } else if (v == "disable") {
    argv = tool("rc-update", scope);
    argv.insert(argv.end(), {"--all", "del", name});
  } else {
    argv = tool("rc-service", scope);
    argv.insert(argv.end(), {"--nocolor", name, v});
  }
  const Output o = run(argv);
  if (o.code == 0) return REPD_OK;
  std::string msg = argv[0] + " " + (v == "enable" ? "add" : v == "disable" ? "del" : v) + " " + name +
                    " failed (exit " + std::to_string(o.code) + ")";
  const std::string& detail = o.err.empty() ? o.out : o.err;
  if (!detail.empty()) msg += ": " + detail.substr(0, detail.find('\n'));
  g_api->set_error(g_host, msg.c_str());
  return REPD_ERR;
}

repd_status enable(void*, const char* unit, const char* scope) {
  return op("enable", unit, scope);
}
repd_status disable(void*, const char* unit, const char* scope) {
  return op("disable", unit, scope);
}
repd_status start(void*, const char* unit, const char* scope) {
  return op("start", unit, scope);
}
repd_status stop(void*, const char* unit, const char* scope) {
  return op("stop", unit, scope);
}

const repd_services_vt kVtable = {probe, state, enable, disable, start, stop};

}  // namespace

extern "C" int repd_plugin_abi(void) {
  return REPD_PLUGIN_ABI;
}

extern "C" int repd_plugin_init(int abi, const repd_host* host, const repd_host_api* api,
                                repd_registrar* reg, const repd_registrar_api* reg_api) {
  if (abi != REPD_PLUGIN_ABI) return 1;
  g_host = host;
  g_api = api;
  return reg_api->register_services(reg, "openrc", REPD_ADDON_VERSION, &kVtable, nullptr);
}
