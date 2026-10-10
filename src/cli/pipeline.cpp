#include "cli/pipeline.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <print>

#include "apply/checks.hpp"
#include "cli/output.hpp"
#include "cli/prompt.hpp"
#include "plan/builder.hpp"
#include "plan/fingerprint.hpp"
#include "plan/graph.hpp"
#include "plugins/loader.hpp"
#include "util/sha256.hpp"
#include "util/strings.hpp"

namespace repro {

Result<std::unique_ptr<Session>> Session::open(const GlobalOptions& g, Mode mode) {
  auto paths = Paths::detect(Env::from_process(), PathsOptions{g.config});
  if (!paths) return std::unexpected(paths.error());
  std::unique_ptr<Session> s(new Session(std::move(*paths), mode));
  s->repo = ConfigRepository::at(s->paths.config_root());
  s->host = std::make_unique<LuaHost>(s->paths, s->fs, mode);
  s->eval = std::make_unique<Evaluator>(*s->host, s->fs, s->repo);
  s->option_flags = g.options;
  return s;
}

std::string Session::option(const std::string& key) const {
  auto it = root.options.find(key);
  return it == root.options.end() ? "auto" : it->second;
}

Result<Value> Session::dispatch_packages(const std::string& op, const List& args) {
  const std::string def = default_package_backend(option("packages.default_backend"));
  auto backend = [&](const std::string& name) -> Result<Backend*> {
    if (Backend* b = backends.find("packages", name)) return b;
    return std::unexpected(BackendRegistry::missing_named("packages", name, "rp.packages", backends.names("packages"), paths));
  };
  if (op == "refresh") {
    if (def.empty()) {
      return fail(ec::kConfig, "rp.packages.refresh() needs a default packages backend",
                  "set rp.option(\"packages.default_backend\", \"<backend>\") in repro.lua");
    }
    auto b = backend(def);
    if (!b) return std::unexpected(b.error());
    return (*b)->refresh().transform([] { return Value{true}; });
  }
  // Group the names by backend, keeping the caller's spelling for the result.
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> groups;  // backend -> (entry, name)
  for (const auto& v : args.empty() ? List{} : args[0].as_list()) {
    const std::string entry = v.as_string();
    PackageRef ref = split_package(entry, def);
    if (ref.backend.empty()) return std::unexpected(package_without_backend("rp.packages." + op, entry, backends.names("packages")));
    groups[ref.backend].emplace_back(entry, std::move(ref.name));
  }
  Map installed;
  for (const auto& [name, items] : groups) {
    auto b = backend(name);
    if (!b) return std::unexpected(b.error());
    std::vector<std::string> names;
    for (const auto& [entry, n] : items) names.push_back(n);
    if (op == "installed") {
      auto r = (*b)->installed(names);
      if (!r) return std::unexpected(r.error());
      for (const auto& [entry, n] : items) installed.emplace(entry, r->contains(n) && r->at(n));
    } else if (op == "install") {
      if (auto r = (*b)->install(names); !r) return std::unexpected(r.error());
    } else if (op == "remove") {
      if (auto r = (*b)->remove(names); !r) return std::unexpected(r.error());
    }
  }
  if (op == "installed") return Value{std::move(installed)};
  return Value{true};
}

namespace {

const std::vector<std::string> kCoreOptions = {"packages.default_backend", "services.backend",
                                               "env.targets"};

}  // namespace

// Options are core keys (checked by repro.lua's rp.option) or "<backend>.<name>"
// keys that installed plugins declare. A repro.lua key for an add-on this host
// lacks is ignored, so one repository serves machines with different add-ons;
// an --option key must exist.
Status Session::apply_options() {
  const auto& declared = backends.options();
  std::vector<std::string> known = kCoreOptions;
  for (const auto& [k, o] : declared) known.push_back(k);
  auto check = [&](const std::string& key, const std::string& value, bool from_flag) -> Status {
    const std::string where = from_flag ? "--option" : "repro.lua";
    if (std::ranges::find(kCoreOptions, key) != kCoreOptions.end()) {
      if (key == "packages.default_backend" && (value.empty() || value == "auto")) {
        return fail(
            ec::kConfig,
            std::format("{}: packages.default_backend needs a backend name (it has no \"auto\")",
                        where),
            "e.g. --option packages.default_backend=pacman");
      }
      return {};
    }
    if (auto it = declared.find(key); it != declared.end()) {
      if (std::ranges::find(it->second.values, value) != it->second.values.end()) return {};
      return fail(ec::kConfig, std::format("{}: option {} cannot be '{}'", where, key, value),
                  std::format("use one of: {} (default {})", join(it->second.values, ", "), it->second.default_value));
    }
    const std::string prefix = key.substr(0, key.find('.'));
    const bool addon_present = std::ranges::any_of(backends.all(), [&](const Backend* b) { return b->info().name == prefix; });
    if (!from_flag && !addon_present) return {};
    if (key == "packages.backend") {
      return fail(
          ec::kConfig,
          std::format("{}: option packages.backend was renamed to packages.default_backend", where),
          std::format("--option packages.default_backend={}", value));
    }
    std::string hint = closest_match(key, known);
    hint = hint.empty() ? std::format("known options: {}", join(known, ", ")) : std::format("did you mean '{}'?", hint);
    return fail(ec::kConfig, std::format("{}: unknown option '{}'", where, key), std::move(hint));
  };
  for (const auto& [key, value] : root.options) {
    if (auto r = check(key, value, false); !r) return r;
  }
  for (const auto& flag : option_flags) {
    const auto eq = flag.find('=');
    if (eq == std::string::npos || eq == 0) {
      return fail(ec::kUsage, std::format("--option expects KEY=VALUE (got '{}')", flag),
                  "example: --option yay.review=skip");
    }
    const std::string key = flag.substr(0, eq), value = flag.substr(eq + 1);
    if (auto r = check(key, value, true); !r) return r;
    root.options[key] = value;
  }
  set_plugin_options(&backends, &root.options);
  return {};
}

Status Session::load() {
  auto r = eval->load_root();
  if (!r) return std::unexpected(r.error());
  root = std::move(*r);
  load_native_plugins(fs, paths, mode, backends);
  auto lua = eval->load_lua_backends();
  if (!lua) return std::unexpected(lua.error());
  for (const auto& b : *lua) {
    BackendInfo info{b.ns, b.name, b.version, "repo", b.path,
                     b.privilege == "user" ? Mode::User : Mode::System, 0, true};
    backends.add(std::make_unique<LuaBackend>(std::move(info), *host, b.can_upgrade, b.can_upgrade_packages));
  }
  if (auto o = apply_options(); !o) return o;
  // rp.packages.* route each name like a `packages` entry; rp.services.* go to
  // the selected services backend.
  host->set_backend_dispatch([this](const std::string& ns, const std::string& op,
                                    const List& args) -> Result<Value> {
    if (ns == "packages") return dispatch_packages(op, args);
    if (!backends.has_candidates(ns)) return std::unexpected(BackendRegistry::missing(ns, "rp." + ns, paths));
    auto b = backends.select(ns, option(ns + ".backend"));
    if (!b) return std::unexpected(BackendRegistry::missing(ns, "rp." + ns, paths));
    Backend* be = *b;
    auto names = [&] {
      std::vector<std::string> out;
      for (const auto& v : args.empty() ? List{} : args[0].as_list()) out.push_back(v.as_string());
      return out;
    };
    auto str = [&](std::size_t i) { return i < args.size() ? args[i].as_string() : std::string(); };
    if (op == "installed") {
      auto r = be->installed(names());
      if (!r) return std::unexpected(r.error());
      Map m;
      for (const auto& [k, v] : *r) m.emplace(k, v);
      return Value{std::move(m)};
    }
    if (op == "install") return be->install(names()).transform([] { return Value{true}; });
    if (op == "remove") return be->remove(names()).transform([] { return Value{true}; });
    if (op == "refresh") return be->refresh().transform([] { return Value{true}; });
    if (op == "state") {
      auto r = be->state(str(0), str(1).empty() ? "system" : str(1));
      if (!r) return std::unexpected(r.error());
      if (!r->has_value()) return Value{};
      return Value{Map{{"enabled", (*r)->enabled}, {"active", (*r)->active}}};
    }
    return be->service(op, str(0), str(1).empty() ? "system" : str(1)).transform([] { return Value{true}; });
  });
  auto mods = eval->load_modules();
  if (!mods) return std::unexpected(mods.error());
  modules = std::move(*mods);
  // Fail-soft per plugin: report refusals, keep going with the others.
  for (const auto& rp : backends.refused()) {
    print_error(Error{std::string(ec::kPluginAbi),
                      rp.abi >= 0 ? std::format("{} built for plugin-abi {}, this repro supports {}", rp.path,
                                                rp.abi, REPRO_PLUGIN_ABI_VERSION)
                                  : std::format("{} was refused: {}", rp.path, rp.reason),
                      "rebuild or update that add-on; other plugins keep working"});
  }
  return Evaluator::validate(root, modules);
}

Result<std::map<std::string, std::string>> parse_provider_flags(const std::vector<std::string>& flags) {
  std::map<std::string, std::string> out;
  for (const auto& f : flags) {
    const auto eq = f.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 == f.size()) {
      return fail(ec::kUsage, std::format("--provider expects IFACE=MODULE (got '{}')", f),
                  "example: --provider dev/ide=dev/ide/vim");
    }
    out[f.substr(0, eq)] = f.substr(eq + 1);
  }
  return out;
}

Result<Scope> select_roots(const Session& s, const PlanRequest& req) {
  Scope scope;
  std::vector<std::string> names;
  for (const auto& p : s.root.profiles) names.push_back(p.name);
  if (req.profile) {
    scope.profile = s.root.find_profile(*req.profile);
    if (scope.profile == nullptr) {
      const std::string c = closest_match(*req.profile, names);
      return fail(ec::kUnknown, std::format("unknown profile '{}'", *req.profile),
                  !c.empty()                ? std::format("did you mean '{}'?", c)
                  : names.empty() ? std::string("declare profiles with rp.profile { ... } in repro.lua")
                                  : "available profiles: " + join(names, ", "));
    }
  } else {
    scope.profile = s.root.default_profile();
  }
  if (scope.profile != nullptr) {
    for (const auto& u : scope.profile->use) {
      if (u.auto_sync) scope.auto_sync[u.name] = *u.auto_sync;
    }
  }
  if (!req.modules.empty()) {
    scope.roots = req.modules;
    scope.root_source = "cli";
    return scope;
  }
  if (scope.profile != nullptr) {
    for (const auto& u : scope.profile->use) {
      if (u.selects()) scope.roots.push_back(u.extras ? u.name + "+" : u.name);
    }
    scope.root_source = "profile";
    return scope;
  }
  if (!s.root.profiles.empty()) {
    return fail(ec::kUsage,
                std::format("no profile selected and none is marked default (profiles: {})", join(names, ", ")),
                std::format("pass --profile {} or set default = true on one profile", names.front()));
  }
  return fail(ec::kUsage, "no modules selected",
              "name modules (repro sync -u MODULE ...) or declare a default profile in repro.lua");
}

namespace {

Value fingerprint_record(const Session& s, const ResolvedModule& m, const ModuleMeta& meta,
                         const Closure& closure, const Plan& plan) {
  Map src_digests;
  // A source directory contributes one digest per file under it.
  std::function<void(const std::string&)> digest_path = [&](const std::string& rel) {
    const std::string path = s.repo.root + "/" + rel;
    if (s.fs.lstat(path).type == FileType::Dir) {  // not through symlinks: they can loop
      if (auto names = s.fs.list(path)) {
        for (const auto& n : *names) digest_path(rel + "/" + n);
      }
    } else if (auto c = s.fs.read(path)) {
      src_digests[rel] = sha256_hex(*c);
    }
  };
  auto digest = [&](const std::optional<std::string>& src) {
    if (src) digest_path(*src);
  };
  for (const auto& f : m.files) digest(f.src_path);
  for (const auto& u : m.udev) digest(u.src_path);
  Map providers;
  for (const auto& r : meta.requires_) {
    if (auto it = closure.providers.find(r); it != closure.providers.end()) {
      providers.emplace(r, it->second.provider);
    }
  }
  Map backends;
  auto use = [&](const std::string& ns, bool used) {
    if (!used) return;
    if (auto it = plan.backends.find(ns); it != plan.backends.end()) {
      backends.emplace(ns, it->second.name + "@" + it->second.version);
    }
  };
  for (const auto& entry : m.packages) {
    use("packages:" + split_package(entry, plan.package_default).backend, true);
  }
  use("services", !m.services.empty());
  return Value{Map{
      {"v", kFingerprintSchema},
      {"repro", REPRO_VERSION},
      {"api", REPRO_LUA_API_VERSION},
      {"mode", std::string(to_string(plan.mode))},
      {"module", m.name},
      {"source", meta.source_sha256},
      {"decl", m.decl},
      {"src_digests", std::move(src_digests)},
      {"params", m.params},
      {"providers", std::move(providers)},
      {"backends", std::move(backends)},
  }};
}

}  // namespace

CheckContext make_check_context(Session& s, const Plan& plan, const PlanRequest& req,
                                const Ledger* ledger, bool* other_store_readable) {
  CheckContext cctx(s.paths, s.fs);
  cctx.mode = s.mode;
  cctx.ledger = ledger;
  cctx.adopt = req.adopt;
  cctx.backends = &s.backends;
  cctx.host = s.host.get();
  cctx.fingerprints = plan.fingerprints;
  cctx.refresh = req.refresh;
  if (ledger != nullptr) cctx.states = ledger->module_states();
  auto other_db = Db::open(s.db_path(other(s.mode)), Db::Access::ReadOnly);
  if (other_db) {
    if (other_store_readable != nullptr) *other_store_readable = true;
    if (other_db->is_open()) cctx.other_states = Ledger(*other_db).module_states();
  }
  Map providers;
  for (const auto& [iface, p] : plan.providers) providers.emplace(iface, p.provider);
  cctx.action_info = Value{Map{{"mode", std::string(to_string(s.mode))},
                               {"home", s.paths.user().home},
                               {"user", s.paths.user().name},
                               {"providers", std::move(providers)}}};
  return cctx;
}


// repro sync --upgrade: one Upgrade step per packages backend this scope uses,
// plus the default backend, owned by the first module that uses it (else the
// first selected module). Backends without an upgrade operation are skipped
// with a warning.
// The step leads the first module (in apply order) that uses the backend.
static Step upgrade_step(const Plan& plan, const Backend& b, UpgradeDesired d,
                         const std::vector<std::string>& candidates) {
  std::string owner = candidates.empty() ? plan.selected.front() : candidates.front();
  for (const auto& m : plan.selected) {
    if (std::ranges::find(candidates, m) == candidates.end() && !candidates.empty()) continue;
    const auto& pkgs = plan.modules.at(m).packages;
    if (std::ranges::any_of(pkgs, [&](const std::string& e) {
          return split_package(e, plan.package_default).backend == d.backend;
        })) {
      owner = m;
      break;
    }
  }
  Step st;
  st.module = owner;
  st.cls = StepClass::Upgrade;
  st.key = d.backend;
  st.id = std::format("{}/upgrade/{}", owner, d.backend);
  st.privilege = b.info().privilege;
  st.desired = std::move(d);
  return st;
}

// --upgrade MODULE…: one targeted upgrade per backend with every package the
// named modules (and "+" extras) declare, so each backend refreshes at most once.
static std::map<std::string, std::vector<Step>> plan_targeted_upgrades(Session& s, Plan& plan) {
  std::map<std::string, std::vector<std::string>> by_backend;
  for (const auto& m : plan.selected) {
    if (std::ranges::find(plan.named, m) == plan.named.end()) continue;
    for (const auto& e : plan.modules.at(m).packages) {
      const auto ref = split_package(e, plan.package_default);
      auto& names = by_backend[ref.backend];
      if (std::ranges::find(names, ref.name) == names.end()) names.push_back(ref.name);
    }
  }
  std::map<std::string, std::vector<Step>> out;
  if (by_backend.empty()) {
    print_warning(std::format("--upgrade: {} declare no packages; nothing to upgrade", join(plan.named, ", ")));
    return out;
  }
  for (auto& [name, names] : by_backend) {
    Backend* b = s.backends.find("packages", name);
    if (b == nullptr) continue;  // reported by the packages step
    if (!b->has_upgrade_packages()) {
      print_warning(std::format("--upgrade: packages backend {} cannot upgrade single packages; skipped {}", name,
                                join(names, ", ")));
      continue;
    }
    if (auto ok = b->can_upgrade_packages(); !ok) {
      // "packages backend pacman: upgrade_packages <reason>" -> "<reason>"
      std::string why = ok.error().message;
      if (const auto at = why.find("upgrade_packages "); at != std::string::npos) why.erase(0, at + 17);
      print_warning(std::format("--upgrade: {}: {}; skipped {}", name, why, join(names, ", ")));
      if (!ok.error().hint.empty()) std::println(stderr, "  hint: {}", ok.error().hint);
      continue;
    }
    Step st = upgrade_step(plan, *b, UpgradeDesired{name, true, std::move(names), {}}, plan.named);
    out[st.module].push_back(std::move(st));
  }
  return out;
}

static Result<std::map<std::string, std::vector<Step>>> plan_upgrades(Session& s, const PlanRequest& req, Plan& plan) {
  std::map<std::string, std::vector<Step>> out;
  if (!req.upgrade) return out;
  if (!req.modules.empty() && !plan.selected.empty()) return plan_targeted_upgrades(s, plan);
  std::vector<std::string> names;
  for (const auto& [key, b] : plan.backends) {
    if (b.ns == "packages") names.push_back(b.name);
  }
  if (!plan.package_default.empty() && std::ranges::find(names, plan.package_default) == names.end()) {
    Backend* b = s.backends.find("packages", plan.package_default);
    if (b == nullptr) {
      return std::unexpected(BackendRegistry::missing_named("packages", plan.package_default, "--upgrade",
                                                            s.backends.names("packages"), s.paths));
    }
    const auto& info = b->info();
    plan.backends["packages:" + info.name] =
        BackendChoice{"packages", info.name, info.version, info.origin, "default", info.privilege};
    names.push_back(info.name);
  }
  if (names.empty() || plan.selected.empty()) {
    print_warning("--upgrade: no packages backend is in use or set as default; nothing to upgrade");
    return out;
  }
  for (const auto& name : names) {
    Backend* b = s.backends.find("packages", name);
    if (b == nullptr) continue;
    if (!b->can_upgrade()) {
      print_warning(std::format("--upgrade: packages backend {} cannot upgrade; skipped", name));
      continue;
    }
    Step st = upgrade_step(plan, *b, UpgradeDesired{name, false, {}, {}}, {});
    out[st.module].push_back(std::move(st));
  }
  return out;
}

Result<PlanOutput> make_plan(Session& s, const PlanRequest& req, const Ledger* ledger) {
  PlanOutput out;
  Plan& plan = out.plan;
  plan.mode = s.mode;
  plan.requested = req.modules;
  plan.refresh = req.refresh;

  auto scope = select_roots(s, req);
  if (!scope) return std::unexpected(scope.error());
  if (scope->profile != nullptr) plan.profile = scope->profile->name;
  for (const auto& r : req.refresh) {
    if (!s.modules.contains(r)) {
      return fail(ec::kUnknown, std::format("--refresh names unknown module '{}'", r), "");
    }
  }

  auto lock = read_lock(*s.host, s.fs, s.repo);
  if (!lock) return std::unexpected(lock.error());
  out.lock = *lock;

  ResolverInputs in;
  in.cli = req.providers;
  if (scope->profile != nullptr) in.profile = scope->profile->providers;
  in.pins = s.root.pins;
  in.lock = lock->providers;
  in.reselect = req.reselect;
  in.interactive = req.interactive;
  in.prompt = prompt_provider;
  for (const auto& iface : req.reselect) {
    if (!s.modules.contains(iface) || !s.modules.at(iface).is_interface()) {
      return fail(ec::kUnknown, std::format("--reselect names '{}', which is not an interface", iface),
                  "list interfaces with `repro list interfaces`");
    }
  }
  Resolver resolver(s.modules, in);
  auto closure = select_scope(s.modules, scope->roots, resolver, scope->root_source, scope->auto_sync);
  // Warnings (e.g. a stale lock entry) matter most when resolution then fails.
  for (const auto& w : resolver.warnings()) print_warning(w);
  if (!closure) return std::unexpected(closure.error());
  plan.selected = closure->order;
  plan.providers = closure->providers;
  plan.named = closure->named;
  plan.optional = closure->optional;
  plan.integrations = closure->integrations;

  // Resolve declarations (params from the active profile's use entries).
  std::vector<const ResolvedModule*> resolved;
  for (const auto& name : plan.selected) {
    Value overrides{Map{}};
    if (scope->profile != nullptr) {
      for (const auto& u : scope->profile->use) {
        if (u.name == name && u.params.is_map()) overrides = u.params;
      }
    }
    auto r = s.eval->resolve(name, overrides);
    if (!r) return std::unexpected(r.error());
    plan.modules.emplace(name, std::move(*r));
  }
  for (const auto& name : plan.selected) resolved.push_back(&plan.modules.at(name));

  // Packages: each entry names its backend ("yay:neovim") or uses the default
  // one from rp.option("packages.default_backend"); there is no automatic choice.
  plan.package_default = default_package_backend(s.option("packages.default_backend"));
  std::map<std::string, std::vector<std::string>> package_users;  // backend -> modules
  for (const auto* m : resolved) {
    for (const auto& entry : m->packages) {
      const PackageRef ref = split_package(entry, plan.package_default);
      if (ref.backend.empty()) {
        return std::unexpected(package_without_backend("module " + m->name, entry, s.backends.names("packages")));
      }
      auto& users = package_users[ref.backend];
      if (std::ranges::find(users, m->name) == users.end()) users.push_back(m->name);
    }
  }
  for (const auto& [name, users] : package_users) {
    Backend* b = s.backends.find("packages", name);
    if (b == nullptr) {
      return std::unexpected(BackendRegistry::missing_named("packages", name, "module " + join(users, ", "),
                                                            s.backends.names("packages"), s.paths));
    }
    const auto& info = b->info();
    plan.backends["packages:" + name] =
        BackendChoice{"packages", info.name, info.version, info.origin,
                      name == plan.package_default ? "default" : "prefix", info.privilege};
  }

  // Services: one backend, by option or by probing (research R17).
  for (const std::string ns : {"services"}) {
    std::vector<std::string> users;
    for (const auto* m : resolved) {
      if (!m->services.empty()) users.push_back(m->name);
    }
    if (users.empty()) continue;
    const std::string who = "module " + join(users, ", ");
    if (!s.backends.has_candidates(ns)) return std::unexpected(BackendRegistry::missing(ns, who, s.paths));
    auto b = s.backends.select(ns, s.option(ns + ".backend"));
    if (!b) {
      if (b.error().message.starts_with("no usable")) {
        return std::unexpected(BackendRegistry::missing(ns, who, s.paths));
      }
      return std::unexpected(b.error());
    }
    const auto& info = (*b)->info();
    plan.backends[ns] = BackendChoice{ns, info.name, info.version, info.origin,
                                      s.backends.how_selected(ns), info.privilege};
  }

  if (auto r = check_conflicts(resolved); !r) return std::unexpected(r.error());
  auto env_targets = resolve_env_targets(s.option("env.targets"), s.paths, s.fs);
  if (!env_targets) return std::unexpected(env_targets.error());
  BuildContext bctx{s.paths, s.fs, s.repo, {}, locale_union(resolved), *env_targets,
                    env_hook_owner(resolved, *env_targets), plan.package_default};
  for (const auto& [key, b] : plan.backends) {
    if (b.ns == "packages") bctx.package_backends[b.name] = b.privilege;
  }
  auto upgrades = plan_upgrades(s, req, plan);
  if (!upgrades) return std::unexpected(upgrades.error());
  std::vector<std::pair<std::string, std::vector<Step>>> module_steps;
  for (const auto& name : plan.selected) {
    auto steps = build_module_steps(plan.modules.at(name), bctx);
    if (!steps) return std::unexpected(steps.error());
    module_steps.emplace_back(name, std::move(*steps));
  }
  batch_packages(module_steps, closure->deps, *upgrades);
  for (auto& [name, steps] : module_steps) {
    // A backend's upgrade runs before any of its installs: it leads the first
    // module (in apply order) that uses the backend, where its first batch is.
    if (auto it = upgrades->find(name); it != upgrades->end()) {
      for (auto& st : it->second) plan.steps.push_back(std::move(st));
    }
    for (auto& st : steps) plan.steps.push_back(std::move(st));
  }
  add_post_steps(plan.steps);

  for (const auto& name : plan.selected) {
    auto fp = fingerprint(*s.host,
                          fingerprint_record(s, plan.modules.at(name), s.modules.at(name), *closure, plan));
    if (!fp) return std::unexpected(fp.error());
    plan.fingerprints[name] = *fp;
  }

  CheckContext cctx = make_check_context(s, plan, req, ledger, &out.other_store_readable);
  if (auto r = check_all(plan.steps, cctx); !r) return std::unexpected(r.error());
  return out;
}

}  // namespace repro
