#include "plan/builder.hpp"

#include <algorithm>
#include <format>
#include <set>

#include "plan/privilege.hpp"
#include "plan/scope.hpp"
#include "util/strings.hpp"

namespace repro {

namespace {

Step make_step(const std::string& module, StepClass cls, std::string key, Mode priv, Desired d) {
  Step s;
  s.module = module;
  s.cls = cls;
  s.key = std::move(key);
  s.id = std::format("{}/{}/{}", module, to_string(cls), s.key);
  s.privilege = priv;
  s.desired = std::move(d);
  s.imperative = cls == StepClass::Action;
  return s;
}

Result<std::string> read_source(const BuildContext& ctx, const std::string& module,
                                 const std::string& src_path, const std::string& what) {
  const std::string path = ctx.repo.root + "/" + src_path;
  if (ctx.fs.stat(path).type != FileType::File) {
    return fail(ec::kConfig, std::format("module {}: {} source {} does not exist", module, what, src_path),
                std::format("create {} in the configuration repository", src_path));
  }
  return ctx.fs.read(path);
}

// Regular files under a source directory, as paths relative to it, sorted.
// Symlinks to files are copied as files; symlinks to directories are refused
// (they can loop, and a copy should not reach outside the module's files/).
Status collect_tree(const BuildContext& ctx, const std::string& module, const std::string& src_path,
                    const std::string& rel, std::vector<std::pair<std::string, FileStat>>& out) {
  const std::string dir = ctx.repo.root + "/" + src_path + (rel.empty() ? "" : "/" + rel);
  auto names = ctx.fs.list(dir);
  if (!names) return std::unexpected(names.error());
  for (const auto& name : *names) {
    const std::string child = rel.empty() ? name : rel + "/" + name;
    const std::string path = dir + "/" + name;
    const FileStat l = ctx.fs.lstat(path);
    const FileStat st = l.type == FileType::Symlink ? ctx.fs.stat(path) : l;
    if (l.type == FileType::Dir) {
      if (auto r = collect_tree(ctx, module, src_path, child, out); !r) return r;
    } else if (st.type == FileType::File) {
      out.emplace_back(child, st);
    } else if (l.type == FileType::Symlink) {
      return fail(ec::kConfig,
                  std::format("module {}: {}/{} is a symlink to a directory or a missing file", module,
                              src_path, child),
                  "replace it with the files themselves, or link the directory with action = \"link\"");
    }
  }
  return {};
}

Result<std::string> logical_dest(const BuildContext& ctx, const std::string& module,
                                 const std::string& dest) {
  const std::string p = normalize_path(ctx.paths.expand(dest));
  if (p.empty() || p == "/") {
    return fail(ec::kConfig, std::format("module {}: invalid destination {}", module, dest),
                "use an absolute path or ~/path");
  }
  return p;
}

}  // namespace

PackageRef split_package(const std::string& entry, const std::string& default_backend) {
  const auto colon = entry.find(':');
  if (colon == std::string::npos) return {default_backend, entry};
  return {entry.substr(0, colon), entry.substr(colon + 1)};
}

std::string default_package_backend(const std::string& option) {
  return option == "auto" ? std::string() : option;
}

Error package_without_backend(const std::string& who, const std::string& entry,
                              const std::vector<std::string>& available) {
  const std::string example = available.empty() ? "<backend>" : available.front();
  std::string hint = std::format("write \"{}:{}\", or set a default in repro.lua: rp.option(\"packages.backend\", \"{}\")",
                                 example, entry, example);
  if (!available.empty()) hint += std::format(" (installed packages backends: {})", join(available, ", "));
  return Error{std::string(ec::kConfig), std::format("{}: package {} names no backend, and no default is set", who, entry),
               std::move(hint)};
}

std::string env_hook_owner(const std::vector<const ResolvedModule*>& modules, const EnvTargets& t) {
  if (std::ranges::find(t.user, "sh") == t.user.end()) return {};
  for (const auto* m : modules) {
    if (!m->env.empty()) return m->name;
  }
  return {};
}

Result<std::vector<Step>> build_module_steps(const ResolvedModule& m, const BuildContext& ctx) {
  std::vector<Step> steps;
  // One packages step per backend the module uses, with that backend's privilege.
  std::map<std::string, std::vector<std::string>> by_backend;
  for (const auto& entry : m.packages) {
    PackageRef ref = split_package(entry, ctx.default_package_backend);
    if (ref.backend.empty()) return std::unexpected(package_without_backend("module " + m.name, entry, {}));
    auto& names = by_backend[ref.backend];
    if (std::ranges::find(names, ref.name) == names.end()) names.push_back(std::move(ref.name));
  }
  for (auto& [backend, names] : by_backend) {
    auto it = ctx.package_backends.find(backend);
    if (it == ctx.package_backends.end()) {
      return fail(ec::kNoBackend,
                  std::format("module {} declares {} packages but that backend is not available", m.name, backend),
                  "");
    }
    PackagesDesired d{std::move(names), {}, backend, {}};
    for (const auto& n : d.names) d.owners[n] = m.name;
    steps.push_back(make_step(m.name, StepClass::Packages, backend, it->second, std::move(d)));
  }
  std::set<std::string> dests;
  for (const auto& f : m.files) {
    auto logical = logical_dest(ctx, m.name, f.dest);
    if (!logical) return std::unexpected(logical.error());
    if (!dests.insert(*logical).second) {
      return fail(ec::kConflict, std::format("module {} declares {} twice", m.name, ctx.paths.display(*logical)),
                  "keep one file entry per destination");
    }
    auto priv = file_privilege(ctx.paths, *logical, f.scope, m.name);
    if (!priv) return std::unexpected(priv.error());
    FileDesired d;
    d.logical = *logical;
    d.physical = ctx.paths.physical(*logical);
    d.kind = f.action;
    d.mode = f.mode;
    d.adopt = f.adopt;
    if (f.src_path) {
      d.source = *f.src_path;
      if (f.action == "link") {
        d.link_target = ctx.repo.root + "/" + *f.src_path;
        if (!ctx.fs.stat(d.link_target).exists()) {
          return fail(ec::kConfig,
                      std::format("module {}: file source {} does not exist", m.name, *f.src_path),
                      std::format("create {} in the configuration repository", *f.src_path));
        }
      } else if (ctx.fs.stat(ctx.repo.root + "/" + *f.src_path).type == FileType::Dir) {
        // A directory copy: one managed file per file in the tree, each with its
        // own drift and adoption handling. Executable files stay executable.
        std::vector<std::pair<std::string, FileStat>> tree;
        if (auto r = collect_tree(ctx, m.name, *f.src_path, "", tree); !r) return std::unexpected(r.error());
        if (tree.empty()) {
          return fail(ec::kConfig, std::format("module {}: source directory {} has no files", m.name, *f.src_path),
                      "add files to it or remove the entry");
        }
        for (const auto& [rel, st] : tree) {
          FileDesired c = d;
          c.tree_root = *logical;
          c.logical = *logical + "/" + rel;
          c.physical = ctx.paths.physical(c.logical);
          c.source = *f.src_path + "/" + rel;
          if (!c.mode && (st.mode & 0111) != 0) c.mode = 0755;
          auto content = ctx.fs.read(ctx.repo.root + "/" + c.source);
          if (!content) return std::unexpected(content.error());
          c.content = std::move(*content);
          if (!dests.insert(c.logical).second) {
            return fail(ec::kConflict,
                        std::format("module {} declares {} twice", m.name, ctx.paths.display(c.logical)),
                        "keep one file entry per destination");
          }
          std::string key = ctx.paths.display(c.logical);
          steps.push_back(make_step(m.name, StepClass::Files, std::move(key), *priv, std::move(c)));
        }
        continue;
      } else {
        auto content = read_source(ctx, m.name, *f.src_path, "file");
        if (!content) return std::unexpected(content.error());
        d.content = std::move(*content);
      }
    } else {
      d.content = f.content.value_or("");
    }
    std::string key = ctx.paths.display(*logical);
    steps.push_back(make_step(m.name, StepClass::Files, std::move(key), *priv, std::move(d)));
  }
  auto env_steps = [&](const std::map<std::string, std::string>& vars, Mode scope,
                       const std::vector<std::string>& targets) {
    std::vector<std::string> kv;
    for (const auto& [k, v] : vars) kv.push_back(k + "=" + v);
    for (const auto& target : targets) {
      FileDesired d;
      d.logical = env_file_path(ctx.paths, target, scope, m.name);
      d.physical = ctx.paths.physical(d.logical);
      d.kind = "env";
      d.content = env_file_content(target, m.name, vars);
      d.vars = join(kv, ", ");
      std::string key = ctx.paths.display(d.logical);
      steps.push_back(make_step(m.name, StepClass::Env, std::move(key), scope, std::move(d)));
    }
  };
  if (!m.env.empty()) env_steps(m.env, Mode::User, ctx.env_targets.user);
  if (!m.system_env.empty()) env_steps(m.system_env, Mode::System, ctx.env_targets.system);
  if (m.name == ctx.env_hook_owner && !ctx.env_targets.profile.empty()) {
    BlockDesired b;
    b.logical = ctx.env_targets.profile;
    b.physical = ctx.paths.physical(b.logical);
    b.lines = sh_profile_block(ctx.paths);
    b.purpose = std::format("load {}/*.sh", ctx.paths.display(sh_env_dir(ctx.paths)));
    std::string key = ctx.paths.display(b.logical);
    steps.push_back(make_step(m.name, StepClass::Env, std::move(key), Mode::User, std::move(b)));
  }
  for (const auto& s : m.services) {
    const std::string key = s.scope == "user" ? "user:" + s.unit : s.unit;
    steps.push_back(make_step(m.name, StepClass::Services, key, service_privilege(s.scope),
                              ServiceDesired{s, {}}));
  }
  for (const auto& u : m.users) {
    steps.push_back(make_step(m.name, StepClass::Users, u.name, Mode::System, UserDesired{u, false, false, {}}));
  }
  if (m.locale) {
    if (!m.locale->locales.empty()) {
      LocaleDesired d;
      d.part = "locales";
      d.locales = m.locale->locales;
      d.all_locales = ctx.all_locales;
      steps.push_back(make_step(m.name, StepClass::Locale, "locales", Mode::System, std::move(d)));
    }
    if (m.locale->default_locale) {
      LocaleDesired d;
      d.part = "default";
      d.default_locale = *m.locale->default_locale;
      steps.push_back(make_step(m.name, StepClass::Locale, "default", Mode::System, std::move(d)));
    }
  }
  for (const auto& r : m.udev) {
    FileDesired d;
    d.logical = "/etc/udev/rules.d/" + r.name;
    d.physical = ctx.paths.physical(d.logical);
    d.kind = "udev";
    if (r.src_path) {
      d.source = *r.src_path;
      auto content = read_source(ctx, m.name, *r.src_path, "udev rule");
      if (!content) return std::unexpected(content.error());
      d.content = std::move(*content);
    } else {
      d.content = r.content.value_or("");
    }
    steps.push_back(make_step(m.name, StepClass::Udev, r.name, Mode::System, std::move(d)));
  }
  for (const auto& a : m.actions) {
    steps.push_back(make_step(m.name, StepClass::Action, a.name, action_privilege(a.mode), ActionDesired{a.name}));
  }
  std::ranges::stable_sort(steps, [](const Step& a, const Step& b) {
    return std::tie(a.cls, a.key) < std::tie(b.cls, b.key);
  });
  return steps;
}

void batch_packages(std::vector<std::pair<std::string, std::vector<Step>>>& modules,
                    const std::map<std::string, std::vector<std::string>>& deps,
                    const std::map<std::string, std::vector<Step>>& extra) {
  std::map<std::string, std::size_t> pos;
  for (std::size_t i = 0; i < modules.size(); ++i) pos[modules[i].first] = i;

  // Transitive dependencies within the scope, memoised.
  std::map<std::string, std::set<std::string>> ancestors;
  auto ancestors_of = [&](auto& self, const std::string& m) -> const std::set<std::string>& {
    if (auto it = ancestors.find(m); it != ancestors.end()) return it->second;
    std::set<std::string> all;
    if (auto it = deps.find(m); it != deps.end()) {
      for (const auto& d : it->second) {
        if (!pos.contains(d)) continue;
        all.insert(d);
        const auto& up = self(self, d);
        all.insert(up.begin(), up.end());
      }
    }
    return ancestors.emplace(m, std::move(all)).first->second;
  };

  // Does module `m` have a step that installs through `backend` must wait for:
  // one in the same pass that is not itself an install through that backend?
  auto orders_before = [&](const std::string& m, const std::string& backend, Mode pass) {
    auto blocks = [&](const Step& s) {
      if (s.privilege != pass) return false;
      return !(s.cls == StepClass::Packages &&
               std::get<PackagesDesired>(s.desired).backend == backend);
    };
    if (std::ranges::any_of(modules[pos.at(m)].second, blocks)) return true;
    auto it = extra.find(m);
    return it != extra.end() && std::ranges::any_of(it->second, blocks);
  };

  std::map<std::string, std::pair<std::size_t, std::size_t>>
      open;  // backend -> (module, step) of its batch
  for (std::size_t i = 0; i < modules.size(); ++i) {
    auto& [name, steps] = modules[i];
    for (std::size_t j = 0; j < steps.size();) {
      if (steps[j].cls != StepClass::Packages) {
        ++j;
        continue;
      }
      auto& d = std::get<PackagesDesired>(steps[j].desired);
      bool join = false;
      if (auto o = open.find(d.backend); o != open.end()) {
        const std::size_t anchor = o->second.first;
        join = std::ranges::none_of(ancestors_of(ancestors_of, name), [&](const std::string& a) {
          return pos.at(a) >= anchor && orders_before(a, d.backend, steps[j].privilege);
        });
      }
      if (!join) {
        open[d.backend] = {i, j};
        ++j;
        continue;
      }
      const auto [bi, bj] = open.at(d.backend);
      auto& into = std::get<PackagesDesired>(modules[bi].second[bj].desired);
      for (auto& n : d.names) {
        if (into.owners.emplace(n, name).second) into.names.push_back(std::move(n));
      }
      steps.erase(steps.begin() + static_cast<std::ptrdiff_t>(j));
    }
  }
}

Status check_conflicts(const std::vector<const ResolvedModule*>& modules) {
  std::map<std::string, std::string> user_vars, system_vars;
  std::optional<std::pair<std::string, std::string>> default_locale;  // value, module
  for (const auto* m : modules) {
    for (const auto& [vars, owners, label] :
         {std::tuple(&m->env, &user_vars, "env"), std::tuple(&m->system_env, &system_vars, "system_env")}) {
      for (const auto& [k, v] : *vars) {
        auto [it, inserted] = owners->emplace(k, m->name);
        if (!inserted) {
          return fail(ec::kConflict,
                      std::format("{} variable {} is set by both {} and {}", label, k, it->second, m->name),
                      std::format("set {} in only one module", k));
        }
      }
    }
    if (m->locale && m->locale->default_locale) {
      if (default_locale && default_locale->first != *m->locale->default_locale) {
        return fail(ec::kConflict,
                    std::format("default locale is {} in {} but {} in {}", default_locale->first,
                                default_locale->second, *m->locale->default_locale, m->name),
                    "declare locale.default in only one module");
      }
      default_locale = {*m->locale->default_locale, m->name};
    }
  }
  return {};
}

std::vector<std::string> locale_union(const std::vector<const ResolvedModule*>& modules) {
  std::set<std::string> all;
  for (const auto* m : modules) {
    if (m->locale) all.insert(m->locale->locales.begin(), m->locale->locales.end());
  }
  return {all.begin(), all.end()};
}

void add_post_steps(std::vector<Step>& steps) {
  auto insert_after_last = [&](StepClass cls, const std::string& what, bool locales_only) {
    std::optional<std::size_t> last;
    for (std::size_t i = 0; i < steps.size(); ++i) {
      if (steps[i].cls != cls) continue;
      if (locales_only && std::get<LocaleDesired>(steps[i].desired).part != "locales") continue;
      last = i;
    }
    if (!last) return;
    Step p = make_step(steps[*last].module, StepClass::Post, what, Mode::System, PostDesired{what, cls});
    p.id = std::format("{}/post/{}", p.module, what);
    // After the owning module's last step.
    std::size_t at = *last + 1;
    while (at < steps.size() && steps[at].module == p.module) ++at;
    steps.insert(steps.begin() + static_cast<std::ptrdiff_t>(at), std::move(p));
  };
  insert_after_last(StepClass::Udev, "udev-reload", false);
  insert_after_last(StepClass::Locale, "locale-gen", true);
}

}  // namespace repro
