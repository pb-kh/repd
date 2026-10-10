#include "plugins/backends.hpp"

#include <algorithm>
#include <format>

#include "eval/host.hpp"
#include "platform/fs.hpp"
#include "util/strings.hpp"

namespace repd {

int origin_rank(const std::string& origin) {
  if (origin == "system") return 0;
  if (origin == "user") return 1;
  return 2;
}

Error Backend::unsupported(std::string_view op) const {
  return Error{std::string(ec::kStep),
               std::format("{} backend {} does not support '{}'", info_.ns, info_.name, op),
               "use a backend that implements it"};
}

Result<std::map<std::string, bool>> Backend::installed(const std::vector<std::string>&) {
  return std::unexpected(unsupported("installed"));
}
Status Backend::install(const std::vector<std::string>&) { return std::unexpected(unsupported("install")); }
Status Backend::remove(const std::vector<std::string>&) { return std::unexpected(unsupported("remove")); }
Status Backend::refresh() { return {}; }
Status Backend::upgrade() { return std::unexpected(unsupported("upgrade")); }
Status Backend::can_upgrade_packages() const { return std::unexpected(unsupported("upgrade_packages")); }
Status Backend::upgrade_packages(const std::vector<std::string>&) {
  return std::unexpected(unsupported("upgrade_packages"));
}
Result<std::optional<ServiceState>> Backend::state(const std::string&, const std::string&) {
  return std::unexpected(unsupported("state"));
}
Status Backend::service(const std::string& op, const std::string&, const std::string&) {
  return std::unexpected(unsupported(op));
}

namespace {

List names_list(const std::vector<std::string>& names) {
  List l;
  for (const auto& n : names) l.emplace_back(n);
  return l;
}

}  // namespace

// Read-only ops run in CHECK, mutating ops in APPLY (contracts/lua-api.md phases).
Result<bool> LuaBackend::probe() {
  PhaseScope p(host_, Phase::Check);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "probe"});
  if (!v) return std::unexpected(v.error());
  return v->as_bool();
}

Result<std::map<std::string, bool>> LuaBackend::installed(const std::vector<std::string>& names) {
  PhaseScope p(host_, Phase::Check);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "installed", names_list(names)});
  if (!v) return std::unexpected(v.error());
  std::map<std::string, bool> out;
  for (const auto& n : names) out[n] = (*v)[n].as_bool();
  return out;
}

Status LuaBackend::install(const std::vector<std::string>& names) {
  PhaseScope p(host_, Phase::Apply);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "install", names_list(names)});
  if (!v) return std::unexpected(v.error());
  return {};
}

Status LuaBackend::remove(const std::vector<std::string>& names) {
  PhaseScope p(host_, Phase::Apply);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "remove", names_list(names)});
  if (!v) return std::unexpected(v.error());
  return {};
}

Status LuaBackend::refresh() {
  PhaseScope p(host_, Phase::Apply);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "refresh"});
  if (!v) return std::unexpected(v.error());
  return {};
}

Status LuaBackend::upgrade() {
  PhaseScope p(host_, Phase::Apply);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "upgrade"});
  if (!v) return std::unexpected(v.error());
  return {};
}

Status LuaBackend::can_upgrade_packages() const {
  if (can_upgrade_packages_) return {};
  return std::unexpected(unsupported("upgrade_packages"));
}

Status LuaBackend::upgrade_packages(const std::vector<std::string>& names) {
  PhaseScope p(host_, Phase::Apply);
  List l;
  for (const auto& n : names) l.emplace_back(n);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "upgrade_packages", Value{std::move(l)}});
  if (!v) return std::unexpected(v.error());
  return {};
}

Result<std::optional<ServiceState>> LuaBackend::state(const std::string& unit,
                                                      const std::string& scope) {
  PhaseScope p(host_, Phase::Check);
  auto v = host_.call("backend_call", {info_.ns, info_.name, "state", unit, scope});
  if (!v) return std::unexpected(v.error());
  if (v->is_nil()) return std::optional<ServiceState>{};
  return std::optional<ServiceState>{ServiceState{(*v)["enabled"].as_bool(), (*v)["active"].as_bool()}};
}

Status LuaBackend::service(const std::string& op, const std::string& unit, const std::string& scope) {
  PhaseScope p(host_, Phase::Apply);
  auto v = host_.call("backend_call", {info_.ns, info_.name, op, unit, scope});
  if (!v) return std::unexpected(v.error());
  return {};
}

void BackendRegistry::add(std::unique_ptr<Backend> b) {
  for (const auto& existing : backends_) {
    if (existing->info().ns == b->info().ns && existing->info().name == b->info().name) {
      duplicates_.push_back(std::format("{} {} from {} (shadowed by {})", b->info().ns,
                                        b->info().name, b->info().path, existing->info().path));
      return;
    }
  }
  backends_.push_back(std::move(b));
  std::ranges::stable_sort(backends_, [](const auto& a, const auto& b) {
    const auto& x = a->info();
    const auto& y = b->info();
    return std::tuple(x.ns, origin_rank(x.origin), x.name) <
           std::tuple(y.ns, origin_rank(y.origin), y.name);
  });
}

bool BackendRegistry::has_candidates(const std::string& ns) const {
  return std::ranges::any_of(backends_, [&](const auto& b) { return b->info().ns == ns; });
}

Backend* BackendRegistry::selected(const std::string& ns) const {
  auto it = selected_.find(ns);
  return it == selected_.end() ? nullptr : it->second;
}

Backend* BackendRegistry::find(const std::string& ns, const std::string& name) const {
  for (const auto& b : backends_) {
    if (b->info().ns == ns && b->info().name == name) return b.get();
  }
  return nullptr;
}

std::vector<std::string> BackendRegistry::names(const std::string& ns) const {
  std::vector<std::string> out;
  for (const auto& b : backends_) {
    if (b->info().ns == ns) out.push_back(b->info().name);
  }
  return out;
}

std::string BackendRegistry::how_selected(const std::string& ns) const {
  auto it = how_.find(ns);
  return it == how_.end() ? "auto" : it->second;
}

std::vector<const Backend*> BackendRegistry::all() const {
  std::vector<const Backend*> out;
  for (const auto& b : backends_) out.push_back(b.get());
  return out;
}

Result<Backend*> BackendRegistry::select(const std::string& ns, const std::string& option) {
  if (auto* b = selected(ns)) return b;
  if (!option.empty() && option != "auto") {
    for (const auto& b : backends_) {
      if (b->info().ns == ns && b->info().name == option) {
        selected_[ns] = b.get();
        how_[ns] = "option";
        return b.get();
      }
    }
    std::vector<std::string> names;
    for (const auto& b : backends_) {
      if (b->info().ns == ns) names.push_back(b->info().name);
    }
    return fail(ec::kNoBackend,
                std::format("repd.lua selects {} backend '{}', which is not installed", ns, option),
                names.empty()
                    ? std::format("install it, or set rp.option(\"{}.backend\", \"auto\")", ns)
                    : std::format("available {} backends: {}", ns, [&] {
                        std::string s;
                        for (const auto& n : names) s += (s.empty() ? "" : ", ") + n;
                        return s;
                      }()));
  }
  for (const auto& b : backends_) {
    if (b->info().ns != ns) continue;
    auto ok = b->probe();
    if (!ok) return std::unexpected(ok.error());
    if (*ok) {
      selected_[ns] = b.get();
      how_[ns] = "auto";
      return b.get();
    }
  }
  return std::unexpected(Error{std::string(ec::kNoBackend), std::format("no usable {} backend", ns), ""});
}

Error BackendRegistry::missing_named(const std::string& ns, const std::string& name, const std::string& who,
                                     const std::vector<std::string>& available, const Paths& paths) {
  RealFs fs;
  // One add-on can register several backends (repd-flatpak: flatpak, flatpak-user).
  const std::string addon = name == "flatpak-user" ? "flatpak" : name;
  std::string hint = fs.stat(paths.physical("/usr/bin/pacman")).exists()
                         ? std::format("install it with `pacman -S repd-{}`", addon)
                         : std::format("install the repd-{} add-on", addon);
  hint += available.empty() ? std::format(", or add a Lua {} backend under plugins/lua/", ns)
                            : std::format(" (installed {} backends: {})", ns, join(available, ", "));
  return Error{std::string(ec::kNoBackend),
               std::format("{} uses {} backend '{}', which is not installed", who, ns, name), std::move(hint)};
}

Error BackendRegistry::missing(const std::string& ns, const std::string& who, const Paths& paths) {
  RealFs fs;
  const bool pacman = fs.stat(paths.physical("/usr/bin/pacman")).exists();
  const bool openrc = fs.stat(paths.physical("/run/openrc")).type == FileType::Dir;
  const bool systemd = !openrc && fs.stat(paths.physical("/usr/bin/systemctl")).exists();
  std::string addon = "repd-pacman";
  if (ns == "services") {
    addon = openrc ? "repd-openrc" : systemd ? "repd-systemd" : "repd-openrc or repd-systemd";
  }
  std::string hint;
  if (pacman && (ns == "packages" || openrc || systemd)) {
    hint = std::format("install it with `pacman -S {}`", addon);
  } else {
    hint = std::format("install a {} backend add-on such as {}, or add a Lua backend under plugins/lua/",
                       ns, addon);
  }
  return Error{std::string(ec::kNoBackend),
               std::format("{} needs a {} backend, but none is installed or usable", who, ns), hint};
}

}  // namespace repd
