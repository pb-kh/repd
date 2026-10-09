#include "eval/declaration.hpp"

#include <cstdlib>

namespace repro {

ModuleMeta ModuleMeta::from_value(const Value& v) {
  ModuleMeta m;
  m.name = v.string_or("name", "");
  m.kind = v.string_or("kind", "module") == "interface" ? ModuleKind::Interface : ModuleKind::Module;
  m.description = v.string_or("description", "");
  m.provides = v.opt_string("provides");
  m.priority = static_cast<int>(v["priority"].as_int(100));
  m.requires_ = v.string_list("requires");
  m.optional_ = v.string_list("optional");
  m.integrates = v.string_list("integrates");
  m.auto_sync = v["auto_sync"].as_bool(true);
  for (const auto& [name, p] : v["params"].as_map()) {
    ParamDecl d;
    d.type = p.string_or("type", "string");
    d.default_value = p["default"];
    d.has_default = p["has_default"].as_bool();
    d.one_of = p["one_of"].as_list();
    d.description = p.string_or("description", "");
    m.params.emplace(name, std::move(d));
  }
  m.has_with_params = v["has_with_params"].as_bool();
  m.source = v.string_or("source", "");
  m.source_sha256 = v.string_or("source_sha256", "");
  return m;
}

ResolvedModule ResolvedModule::from_value(const Value& v) {
  ResolvedModule r;
  r.name = v.string_or("name", "");
  r.params = v["params"].as_map();
  r.packages = v.string_list("packages");
  for (const auto& f : v["files"].as_list()) {
    FileEntry e;
    e.dest = f.string_or("dest", "");
    e.src = f.opt_string("src");
    e.src_path = f.opt_string("src_path");
    e.content = f.opt_string("content");
    e.action = f.string_or("action", e.src ? "link" : "create");
    if (auto mode = f.opt_string("mode")) e.mode = static_cast<unsigned>(std::strtoul(mode->c_str(), nullptr, 8));
    e.scope = f.opt_string("scope");
    e.adopt = f["adopt"].as_bool();
    r.files.push_back(std::move(e));
  }
  r.env = v.string_map("env");
  r.system_env = v.string_map("system_env");
  for (const auto& s : v["services"].as_list()) {
    ServiceEntry e;
    e.unit = s.string_or("unit", "");
    e.scope = s.string_or("scope", "system");
    e.state = s.string_or("state", "enabled");
    if (s["active"].is_bool()) e.active = s["active"].as_bool();
    r.services.push_back(std::move(e));
  }
  for (const auto& u : v["users"].as_list()) {
    UserEntry e;
    e.name = u.string_or("name", "");
    e.shell = u.opt_string("shell");
    e.groups = u.string_list("groups");
    r.users.push_back(std::move(e));
  }
  if (v["locale"].is_map() && !v["locale"].as_map().empty()) {
    LocaleEntry e;
    e.locales = v["locale"].string_list("locales");
    e.default_locale = v["locale"].opt_string("default");
    r.locale = std::move(e);
  }
  for (const auto& u : v["udev"].as_list()) {
    UdevEntry e;
    e.name = u.string_or("name", "");
    e.src = u.opt_string("src");
    e.src_path = u.opt_string("src_path");
    e.content = u.opt_string("content");
    r.udev.push_back(std::move(e));
  }
  for (const auto& a : v["actions"].as_list()) {
    r.actions.push_back(ActionDecl{a.string_or("name", ""), a.string_or("mode", "user")});
  }
  r.decl = v;
  return r;
}

const Profile* RootConfig::find_profile(const std::string& name) const {
  for (const auto& p : profiles) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

const Profile* RootConfig::default_profile() const {
  for (const auto& p : profiles) {
    if (p.is_default) return &p;
  }
  return nullptr;
}

RootConfig RootConfig::from_value(const Value& v) {
  RootConfig c;
  c.options = v.string_map("options");
  c.pins = v.string_map("pins");
  for (const auto& p : v["profiles"].as_list()) {
    Profile prof;
    prof.name = p.string_or("name", "");
    prof.description = p.string_or("description", "");
    prof.is_default = p["default"].as_bool();
    for (const auto& u : p["use"].as_list()) {
      UseEntry e{u.string_or("name", ""), u["params"], false, std::nullopt};
      if (u["auto_sync"].is_bool()) e.auto_sync = u["auto_sync"].as_bool();
      prof.use.push_back(std::move(e));
    }
    prof.providers = p.string_map("providers");
    c.profiles.push_back(std::move(prof));
  }
  return c;
}

}  // namespace repro
