// repro-systemd: services backend for systemd (contracts/plugin-abi.md).
// `systemctl [--user] is-enabled/is-active/enable/disable/start/stop -- unit`.
// User-scope units are refused under root: they belong to the user pass.
#include <repro/plugin.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

const repro_host* g_host = nullptr;
const repro_host_api* g_api = nullptr;

struct Output {
  int code = 0;
  std::string out;
  std::string err;
};

Output systemctl(const char* scope, const char* verb, const char* unit) {
  std::vector<std::string> argv = {"systemctl"};
  if (std::string(scope) == "user") argv.emplace_back("--user");
  argv.emplace_back(verb);
  argv.emplace_back("--");
  argv.emplace_back(unit);
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
  while (!o.out.empty() && (o.out.back() == '\n' || o.out.back() == ' ')) o.out.pop_back();
  return o;
}

bool refuse_user_scope_as_root(const char* scope) {
  if (std::string(scope) == "user" && ::geteuid() == 0) {
    g_api->set_error(g_host, "user services belong to the user pass (run: repro sync -u)");
    return true;
  }
  return false;
}

bool is_dir(const char* path) {
  struct stat st{};
  return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// systemctl can be installed on a host that booted another init (OpenRC).
int probe(void*) {
  if (is_dir("/run/openrc") && !is_dir("/run/systemd/system")) return 0;
  return ::access("/usr/bin/systemctl", X_OK) == 0 || ::access("/bin/systemctl", X_OK) == 0 ? 1 : 0;
}

repro_status state(void*, const char* unit, const char* scope, int* enabled, int* active) {
  if (refuse_user_scope_as_root(scope)) return REPRO_ERR;
  const Output e = systemctl(scope, "is-enabled", unit);
  if (e.out.empty() || e.out == "not-found" || e.err.find("No such file") != std::string::npos) {
    *enabled = -1;
    *active = 0;
    return REPRO_OK;
  }
  *enabled = (e.out == "enabled" || e.out == "enabled-runtime" || e.out == "alias") ? 1 : 0;
  const Output a = systemctl(scope, "is-active", unit);
  *active = a.out == "active" ? 1 : 0;
  return REPRO_OK;
}

repro_status op(const char* verb, const char* unit, const char* scope) {
  if (refuse_user_scope_as_root(scope)) return REPRO_ERR;
  const Output o = systemctl(scope, verb, unit);
  if (o.code == 0) return REPRO_OK;
  std::string msg = std::string("systemctl ") + verb + " " + unit + " failed (exit " + std::to_string(o.code) + ")";
  if (!o.err.empty()) msg += ": " + o.err.substr(0, o.err.find('\n'));
  g_api->set_error(g_host, msg.c_str());
  return REPRO_ERR;
}

repro_status enable(void*, const char* unit, const char* scope) { return op("enable", unit, scope); }
repro_status disable(void*, const char* unit, const char* scope) { return op("disable", unit, scope); }
repro_status start(void*, const char* unit, const char* scope) { return op("start", unit, scope); }
repro_status stop(void*, const char* unit, const char* scope) { return op("stop", unit, scope); }

const repro_services_vt kVtable = {probe, state, enable, disable, start, stop};

}  // namespace

extern "C" int repro_plugin_abi(void) { return REPRO_PLUGIN_ABI; }

extern "C" int repro_plugin_init(int abi, const repro_host* host, const repro_host_api* api,
                                 repro_registrar* reg, const repro_registrar_api* reg_api) {
  if (abi != REPRO_PLUGIN_ABI) return 1;
  g_host = host;
  g_api = api;
  return reg_api->register_services(reg, "systemd", REPRO_ADDON_VERSION, &kVtable, nullptr);
}
