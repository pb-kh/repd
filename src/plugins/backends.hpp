// Backend abstraction (research R17, data-model.md "Backend"): package and
// service engines are either native .so add-ons or repo-local Lua backends.
// The registry ranks candidates (origin: system > user > repo, then name) and
// selects one per namespace, probing lazily.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/paths.hpp"
#include "util/errors.hpp"

namespace repro {

class LuaHost;

struct BackendInfo {
  std::string ns;       // packages|services
  std::string name;
  std::string version;
  std::string origin;   // system|user|repo
  std::string path;     // display path
  Mode privilege = Mode::System;  // packages: which pass owns its steps
  int abi = 0;          // native only
  bool lua = false;
};

struct ServiceState {
  bool enabled = false;
  bool active = false;
};

class Backend {
 public:
  explicit Backend(BackendInfo info) : info_(std::move(info)) {}
  virtual ~Backend() = default;
  const BackendInfo& info() const { return info_; }

  virtual Result<bool> probe() = 0;
  // packages
  virtual Result<std::map<std::string, bool>> installed(const std::vector<std::string>& names);
  virtual Status install(const std::vector<std::string>& names);
  virtual Status remove(const std::vector<std::string>& names);
  virtual Status refresh();
  virtual bool can_upgrade() const { return false; }
  virtual Status upgrade();  // full upgrade (repro sync --upgrade)
  // Targeted upgrade (repro sync --upgrade MODULE…): implemented at all, may it
  // run now (the error says why not), and the upgrade itself.
  virtual bool has_upgrade_packages() const { return false; }
  virtual Status can_upgrade_packages() const;
  virtual Status upgrade_packages(const std::vector<std::string>& names);
  // services; nullopt = unit unknown
  virtual Result<std::optional<ServiceState>> state(const std::string& unit, const std::string& scope);
  virtual Status service(const std::string& op, const std::string& unit, const std::string& scope);

 protected:
  Error unsupported(std::string_view op) const;
  BackendInfo info_;
};

// Repo-local backend implemented in Lua (plugins/lua/*.lua), reduced trust.
class LuaBackend final : public Backend {
 public:
  LuaBackend(BackendInfo info, LuaHost& host, bool can_upgrade = false, bool can_upgrade_packages = false)
      : Backend(std::move(info)),
        host_(host),
        can_upgrade_(can_upgrade),
        can_upgrade_packages_(can_upgrade_packages) {}
  Result<bool> probe() override;
  Result<std::map<std::string, bool>> installed(const std::vector<std::string>& names) override;
  Status install(const std::vector<std::string>& names) override;
  Status remove(const std::vector<std::string>& names) override;
  Status refresh() override;
  bool can_upgrade() const override { return can_upgrade_; }
  Status upgrade() override;
  bool has_upgrade_packages() const override { return can_upgrade_packages_; }
  Status can_upgrade_packages() const override;
  Status upgrade_packages(const std::vector<std::string>& names) override;
  Result<std::optional<ServiceState>> state(const std::string& unit, const std::string& scope) override;
  Status service(const std::string& op, const std::string& unit, const std::string& scope) override;

 private:
  LuaHost& host_;
  bool can_upgrade_;
  bool can_upgrade_packages_;
};

// An option a plugin declared (repro_registrar_api.declare_option).
struct PluginOption {
  std::string key;  // "<backend>.<name>"
  std::vector<std::string> values;
  std::string default_value;
  std::string path;  // declaring plugin
};

struct RefusedPlugin {
  std::string path;
  std::string reason;
  int abi = -1;  // plugin-abi the plugin reported, if any
};

class BackendRegistry {
 public:
  void add(std::unique_ptr<Backend> b);
  void refuse(std::string path, std::string reason, int abi = -1) {
    refused_.push_back({std::move(path), std::move(reason), abi});
  }

  // option: "auto" (or empty) probes candidates in rank order; otherwise the named backend.
  Result<Backend*> select(const std::string& ns, const std::string& option);
  Backend* selected(const std::string& ns) const;
  // A backend by name, without selecting it (packages are routed per entry).
  Backend* find(const std::string& ns, const std::string& name) const;
  std::vector<std::string> names(const std::string& ns) const;  // rank order
  bool has_candidates(const std::string& ns) const;
  std::string how_selected(const std::string& ns) const;  // "auto" or "option"

  std::vector<const Backend*> all() const;  // rank order
  const std::vector<RefusedPlugin>& refused() const { return refused_; }
  const std::vector<std::string>& duplicates() const { return duplicates_; }

  void declare_option(PluginOption o) { options_.emplace(o.key, std::move(o)); }
  const std::map<std::string, PluginOption>& options() const { return options_; }

  // Message naming the add-on to install (research R17 hint table).
  static Error missing(const std::string& ns, const std::string& who, const Paths& paths);
  // A backend named by a package prefix or the option that is not installed.
  static Error missing_named(const std::string& ns, const std::string& name, const std::string& who,
                             const std::vector<std::string>& available, const Paths& paths);

 private:
  std::vector<std::unique_ptr<Backend>> backends_;
  std::map<std::string, Backend*> selected_;
  std::map<std::string, std::string> how_;
  std::vector<RefusedPlugin> refused_;
  std::vector<std::string> duplicates_;
  std::map<std::string, PluginOption> options_;
};

int origin_rank(const std::string& origin);

}  // namespace repro
