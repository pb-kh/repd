#include "eval/discovery.hpp"

#include <algorithm>
#include <format>

#include "plan/scope.hpp"

namespace repd {

Status check_repository(const Fs& fs, const ConfigRepository& repo) {
  if (fs.stat(repo.entry).type != FileType::File) {
    return fail(
        ec::kConfig, std::format("no repd.lua in {}", repo.root),
        fs.stat(repo.root).exists()
            ? "create repd.lua (see `repd init`) or pass --config PATH"
            : "run `repd init` to scaffold a configuration repository, or pass --config PATH");
  }
  return {};
}

namespace {

Status walk(const Fs& fs, const std::string& dir, const std::string& rel_dir,
            std::map<std::string, DiscoveredModule>& out) {
  auto names = fs.list(dir);
  if (!names) return {};
  // A directory module (init.lua) keeps its sources in files/, which may contain
  // any file type, including .lua; it is never scanned for modules.
  const bool module_dir = std::ranges::find(*names, "init.lua") != names->end();
  for (const auto& entry : *names) {
    const std::string path = dir + "/" + entry;
    const std::string rel = rel_dir.empty() ? entry : rel_dir + "/" + entry;
    const FileType t = fs.stat(path).type;
    if (t == FileType::Dir) {
      if (module_dir && entry == "files") continue;
      if (auto r = walk(fs, path, rel, out); !r) return r;
      continue;
    }
    if (t != FileType::File || !entry.ends_with(".lua")) continue;
    const std::string name = module_name_from_path(rel);
    if (!valid_module_name(name)) {
      return fail(ec::kName, std::format("modules/{} does not give a valid module name", rel),
                  "module paths use lowercase letters, digits, _ + - and '/', e.g. modules/dev/git.lua");
    }
    if (auto it = out.find(name); it != out.end()) {
      return fail(ec::kCollision,
                  std::format("module {} is defined twice: {} and modules/{}", name, it->second.rel, rel),
                  "keep either modules/" + name + ".lua or modules/" + name + "/init.lua");
    }
    out.emplace(name, DiscoveredModule{name, path, "modules/" + rel});
  }
  return {};
}

}  // namespace

Result<std::map<std::string, DiscoveredModule>> discover_modules(const Fs& fs,
                                                                 const ConfigRepository& repo) {
  std::map<std::string, DiscoveredModule> out;
  if (fs.stat(repo.modules_dir).type != FileType::Dir) return out;
  if (auto r = walk(fs, repo.modules_dir, "", out); !r) return std::unexpected(r.error());
  return out;
}

std::vector<DiscoveredModule> discover_lua_plugins(const Fs& fs, const ConfigRepository& repo) {
  std::vector<DiscoveredModule> out;
  auto names = fs.list(repo.lua_plugins_dir);
  if (!names) return out;
  for (const auto& n : *names) {
    if (!n.ends_with(".lua")) continue;
    const std::string path = repo.lua_plugins_dir + "/" + n;
    if (fs.stat(path).type != FileType::File) continue;
    out.push_back(DiscoveredModule{n.substr(0, n.size() - 4), path, "plugins/lua/" + n});
  }
  return out;
}

}  // namespace repd
