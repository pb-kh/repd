// C++ view of the evaluated declaration model (data-model.md §1). Built from
// the plain data trees the Lua bootstrap returns; C++ never parses config.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "util/value.hpp"

namespace repro {

enum class ModuleKind { Module, Interface };

struct ParamDecl {
  std::string type;
  Value default_value;
  bool has_default = false;
  List one_of;
  std::string description;
};

struct ModuleMeta {
  std::string name;
  ModuleKind kind = ModuleKind::Module;
  std::string description;
  std::optional<std::string> provides;
  int priority = 100;
  std::vector<std::string> requires_;    // synced along with this module
  std::vector<std::string> optional_;    // requires { "x", optional = true }: listed, synced when asked for
  std::vector<std::string> integrates;   // an integration: synced once all of these are
  bool auto_sync = true;                 // false: integrations wait to be named
  std::map<std::string, ParamDecl> params;
  bool has_with_params = false;
  std::string source;  // path relative to the config repository
  std::string source_sha256;

  bool is_interface() const { return kind == ModuleKind::Interface; }
  static ModuleMeta from_value(const Value& v);
};

struct FileEntry {
  std::string dest;                      // as declared (absolute or ~/...)
  std::optional<std::string> src;        // as declared
  std::optional<std::string> src_path;   // repo-relative resolved source
  std::optional<std::string> content;
  std::string action;                    // link|copy|create
  std::optional<unsigned> mode;
  std::optional<std::string> scope;      // user|system
  bool adopt = false;
};

struct ServiceEntry {
  std::string unit;
  std::string scope = "system";
  std::string state;  // enabled|disabled
  std::optional<bool> active;
};

struct UserEntry {
  std::string name;
  std::optional<std::string> shell;
  std::vector<std::string> groups;
};

struct LocaleEntry {
  std::vector<std::string> locales;
  std::optional<std::string> default_locale;
};

struct UdevEntry {
  std::string name;
  std::optional<std::string> src;
  std::optional<std::string> src_path;
  std::optional<std::string> content;
};

struct ActionDecl {
  std::string name;
  std::string mode;  // user|system
};

struct ResolvedModule {
  std::string name;
  Map params;
  std::vector<std::string> packages;
  std::vector<FileEntry> files;
  std::map<std::string, std::string> env;
  std::map<std::string, std::string> system_env;
  std::vector<ServiceEntry> services;
  std::vector<UserEntry> users;
  std::optional<LocaleEntry> locale;
  std::vector<UdevEntry> udev;
  std::vector<ActionDecl> actions;
  Value decl;  // full plain declaration (fingerprint, `repro show`)

  static ResolvedModule from_value(const Value& v);
};

struct UseEntry {
  std::string name;               // without a trailing "+"
  Value params;                   // map
  bool extras = false;            // "name+": with its optional requires
  std::optional<bool> auto_sync;  // { "name", auto_sync = false }: an override, not a selection

  bool selects() const { return auto_sync.value_or(true); }
};

struct Profile {
  std::string name;
  std::string description;
  bool is_default = false;
  std::vector<UseEntry> use;
  std::map<std::string, std::string> providers;
};

struct RootConfig {
  std::map<std::string, std::string> options;
  std::map<std::string, std::string> pins;
  std::vector<Profile> profiles;

  const Profile* find_profile(const std::string& name) const;
  const Profile* default_profile() const;
  static RootConfig from_value(const Value& v);
};

// A repo-local Lua backend as loaded by the bootstrap.
struct LuaBackendSpec {
  std::string ns;
  std::string name;
  std::string privilege;  // user|system
  std::string version;
  std::string path;  // repo-relative
  bool can_upgrade = false;           // packages: ops.upgrade is defined
  bool can_upgrade_packages = false;  // packages: ops.upgrade_packages is defined
};

}  // namespace repro
