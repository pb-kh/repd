#include "plugins/loader.hpp"

#include <dlfcn.h>
#include <repd/plugin.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

#include "platform/proc.hpp"
#include "util/strings.hpp"

// Opaque handle types from plugin.h; the host and registrar are ours.
struct repd_host {
  std::string last_error;
  const repd::BackendRegistry* registry = nullptr;               // declared options
  const std::map<std::string, std::string>* options = nullptr;   // effective rp.option values
};
struct repd_registrar {
  repd::BackendRegistry* registry;
  std::string path;
  std::string origin;
  int registered = 0;
};

namespace repd {
namespace {

std::string* g_log_sink = nullptr;
repd_host g_host;

char* dup_str(const std::string& s) {
  char* p = static_cast<char*>(std::malloc(s.size() + 1));
  std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

int api_exec(const repd_host*, const char* const* argv, char** out, char** err) {
  std::vector<std::string> args;
  for (const char* const* a = argv; a != nullptr && *a != nullptr; ++a) args.emplace_back(*a);
  auto r = run_process(args);
  if (!r) {
    if (out) *out = dup_str("");
    if (err) *err = dup_str(r.error().message);
    return 127;
  }
  if (g_log_sink != nullptr) {
    g_log_sink->append(std::format("$ {}\n{}{}", args.empty() ? "" : args[0], tail(r->out, 2048),
                                   tail(r->err, 2048)));
  }
  if (out) *out = dup_str(r->out);
  if (err) *err = dup_str(r->err);
  return r->code;
}

// Interactive commands (sudo prompts, AUR builds) talk to the user directly; the
// journal only records the command line and exit status.
int api_exec_tty(const repd_host*, const char* const* argv) {
  std::vector<std::string> args;
  for (const char* const* a = argv; a != nullptr && *a != nullptr; ++a) args.emplace_back(*a);
  std::string line;
  for (const auto& a : args) line += (line.empty() ? "" : " ") + a;
  std::fflush(stdout);
  std::fprintf(stderr, "  $ %s\n", line.c_str());
  std::fflush(stderr);
  ProcOptions opts;
  opts.inherit_input = true;
  opts.inherit_output = true;
  auto r = run_process(args, opts);
  const int code = r ? r->code : 127;
  if (!r) std::fprintf(stderr, "  %s\n", r.error().message.c_str());
  if (g_log_sink != nullptr) g_log_sink->append(std::format("$ {}  (on the terminal, exit {})\n", line, code));
  return code;
}

void api_log(const repd_host*, const char* msg) {
  if (g_log_sink != nullptr && msg != nullptr) {
    if (!g_log_sink->empty() && g_log_sink->back() != '\n') g_log_sink->push_back('\n');
    g_log_sink->append(msg);
  }
}

void api_set_error(const repd_host*, const char* msg) {
  g_host.last_error = msg ? msg : "";
}

void api_free_str(char* s) { std::free(s); }

const char* api_option(const repd_host*, const char* key) {
  if (key == nullptr || g_host.registry == nullptr) return nullptr;
  const auto& declared = g_host.registry->options();
  auto d = declared.find(key);
  if (d == declared.end()) return nullptr;
  if (g_host.options != nullptr) {
    if (auto it = g_host.options->find(key); it != g_host.options->end()) return it->second.c_str();
  }
  return d->second.default_value.c_str();
}

const repd_host_api kHostApi = {api_exec,
                                api_log,
                                api_set_error,
                                api_free_str,
#if REPD_TEST_HOOKS
                                1,
#else
                                0,
#endif
                                api_exec_tty,
                                api_option};

Error native_error(const BackendInfo& info, std::string_view op) {
  // set_error("message\nhint") — an optional second line replaces the generic hint.
  std::string msg = g_host.last_error.empty() ? "failed" : g_host.last_error;
  g_host.last_error.clear();
  std::string hint = "see the step's journal output (repd journal) for details";
  if (const auto nl = msg.find('\n'); nl != std::string::npos) {
    if (nl + 1 < msg.size()) hint = msg.substr(nl + 1);
    msg.resize(nl);
  }
  return Error{std::string(ec::kStep), std::format("{} backend {}: {} {}", info.ns, info.name, op, msg),
               std::move(hint)};
}

std::vector<const char*> c_names(const std::vector<std::string>& names) {
  std::vector<const char*> out;
  for (const auto& n : names) out.push_back(n.c_str());
  out.push_back(nullptr);
  return out;
}

class NativePackages final : public Backend {
 public:
  NativePackages(BackendInfo info, const repd_packages_vt* vt, void* ud)
      : Backend(std::move(info)), vt_(*vt), ud_(ud) {}
  Result<bool> probe() override { return vt_.probe != nullptr && vt_.probe(ud_) == 1; }
  Result<std::map<std::string, bool>> installed(const std::vector<std::string>& names) override {
    std::vector<unsigned char> flags(names.size(), 0);
    auto c = c_names(names);
    if (vt_.installed(ud_, c.data(), names.size(), flags.data()) != REPD_OK) {
      return std::unexpected(native_error(info_, "installed"));
    }
    std::map<std::string, bool> out;
    for (std::size_t i = 0; i < names.size(); ++i) out[names[i]] = flags[i] != 0;
    return out;
  }
  Status install(const std::vector<std::string>& names) override {
    auto c = c_names(names);
    if (vt_.install(ud_, c.data(), names.size()) != REPD_OK)
      return std::unexpected(native_error(info_, "install"));
    return {};
  }
  Status remove(const std::vector<std::string>& names) override {
    if (vt_.remove == nullptr) return std::unexpected(unsupported("remove"));
    auto c = c_names(names);
    if (vt_.remove(ud_, c.data(), names.size()) != REPD_OK)
      return std::unexpected(native_error(info_, "remove"));
    return {};
  }
  bool can_upgrade() const override { return vt_.upgrade != nullptr; }
  Status upgrade() override {
    if (vt_.upgrade == nullptr) return std::unexpected(unsupported("upgrade"));
    if (vt_.upgrade(ud_) != REPD_OK) return std::unexpected(native_error(info_, "upgrade"));
    return {};
  }
  bool has_upgrade_packages() const override { return vt_.upgrade_packages != nullptr; }
  Status can_upgrade_packages() const override {
    if (vt_.upgrade_packages == nullptr) return std::unexpected(unsupported("upgrade_packages"));
    if (vt_.can_upgrade_packages == nullptr || vt_.can_upgrade_packages(ud_) == 1) return {};
    return std::unexpected(native_error(info_, "upgrade_packages"));
  }
  Status upgrade_packages(const std::vector<std::string>& names) override {
    if (vt_.upgrade_packages == nullptr) return std::unexpected(unsupported("upgrade_packages"));
    auto c = c_names(names);
    if (vt_.upgrade_packages(ud_, c.data(), names.size()) != REPD_OK) {
      return std::unexpected(native_error(info_, "upgrade_packages"));
    }
    return {};
  }
  Status refresh() override {
    if (vt_.refresh == nullptr) return {};
    if (vt_.refresh(ud_) != REPD_OK) return std::unexpected(native_error(info_, "refresh"));
    return {};
  }

 private:
  repd_packages_vt vt_;
  void* ud_;
};

class NativeServices final : public Backend {
 public:
  NativeServices(BackendInfo info, const repd_services_vt* vt, void* ud)
      : Backend(std::move(info)), vt_(*vt), ud_(ud) {}
  Result<bool> probe() override { return vt_.probe != nullptr && vt_.probe(ud_) == 1; }
  Result<std::optional<ServiceState>> state(const std::string& unit, const std::string& scope) override {
    int enabled = 0, active = 0;
    if (vt_.state(ud_, unit.c_str(), scope.c_str(), &enabled, &active) != REPD_OK) {
      return std::unexpected(native_error(info_, "state"));
    }
    if (enabled < 0) return std::optional<ServiceState>{};
    return std::optional<ServiceState>{ServiceState{enabled == 1, active == 1}};
  }
  Status service(const std::string& op, const std::string& unit, const std::string& scope) override {
    using Fn = repd_status (*)(void*, const char*, const char*);
    Fn fn = op == "enable"    ? vt_.enable
            : op == "disable" ? vt_.disable
            : op == "start"   ? vt_.start
            : op == "stop"    ? vt_.stop
                              : nullptr;
    if (fn == nullptr) return std::unexpected(unsupported(op));
    if (fn(ud_, unit.c_str(), scope.c_str()) != REPD_OK)
      return std::unexpected(native_error(info_, op));
    return {};
  }

 private:
  repd_services_vt vt_;
  void* ud_;
};

int reg_packages(repd_registrar* r, const char* name, const char* version, repd_privilege priv,
                 const repd_packages_vt* vt, void* ud) {
  if (r == nullptr || name == nullptr || vt == nullptr || vt->installed == nullptr ||
      vt->install == nullptr) {
    return 1;
  }
  BackendInfo info{"packages",      name,    version ? version : "?",
                   r->origin,       r->path, priv == REPD_PRIV_USER ? Mode::User : Mode::System,
                   REPD_PLUGIN_ABI, false};
  r->registry->add(std::make_unique<NativePackages>(std::move(info), vt, ud));
  ++r->registered;
  return 0;
}

int reg_services(repd_registrar* r, const char* name, const char* version,
                 const repd_services_vt* vt, void* ud) {
  if (r == nullptr || name == nullptr || vt == nullptr || vt->state == nullptr) return 1;
  BackendInfo info{"services", name,         version ? version : "?", r->origin,
                   r->path,    Mode::System, REPD_PLUGIN_ABI,         false};
  r->registry->add(std::make_unique<NativeServices>(std::move(info), vt, ud));
  ++r->registered;
  return 0;
}

int reg_option(repd_registrar* r, const char* key, const char* values, const char* default_value) {
  if (r == nullptr || key == nullptr || values == nullptr || default_value == nullptr) return 1;
  const std::string k = key;
  const auto dot = k.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 == k.size()) return 1;
  PluginOption o{k, split(values, ','), default_value, r->path};
  if (std::ranges::find(o.values, o.default_value) == o.values.end()) return 1;
  r->registry->declare_option(std::move(o));
  return 0;
}

const repd_registrar_api kRegistrarApi = {reg_packages, reg_services, reg_option};

}  // namespace

void set_plugin_log_sink(std::string* sink) { g_log_sink = sink; }

bool is_plugin_file(std::string_view name) {
  return name.starts_with("librepd-") && name.ends_with(".so") && name.size() > 11;
}

void set_plugin_options(const BackendRegistry* registry, const std::map<std::string, std::string>* options) {
  g_host.registry = registry;
  g_host.options = options;
}

void load_native_plugins(const Fs& fs, const Paths& paths, Mode mode, BackendRegistry& registry) {
  for (const auto& [dir, origin] : paths.plugin_dirs(mode)) {
    auto names = fs.list(dir);
    if (!names) continue;
    for (const auto& n : *names) {
      if (!is_plugin_file(n)) continue;
      const std::string path = dir + "/" + n;
      void* h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (h == nullptr) {
        const char* e = ::dlerror();
        registry.refuse(path, std::format("cannot load: {}", e ? e : "?"));
        continue;
      }
      auto abi_fn = reinterpret_cast<int (*)()>(::dlsym(h, "repd_plugin_abi"));
      auto init = reinterpret_cast<decltype(&repd_plugin_init)>(::dlsym(h, "repd_plugin_init"));
      if (init == nullptr) {
        registry.refuse(path, "not a repd plugin (no repd_plugin_init)");
        ::dlclose(h);
        continue;
      }
      if (abi_fn != nullptr && abi_fn() != REPD_PLUGIN_ABI) {
        registry.refuse(path, std::format("plugin-abi {} ≠ {}", abi_fn(), REPD_PLUGIN_ABI),
                        abi_fn());
        ::dlclose(h);
        continue;
      }
      repd_registrar reg{&registry, path, origin, 0};
      if (init(REPD_PLUGIN_ABI, &g_host, &kHostApi, &reg, &kRegistrarApi) != 0) {
        registry.refuse(path, "initialisation failed (incompatible plugin-abi?)");
        continue;  // keep it loaded: it may have registered partially
      }
      // Plugins stay loaded for the life of the process.
    }
  }
}

}  // namespace repd
