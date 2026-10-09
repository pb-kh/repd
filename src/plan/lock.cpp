#include "plan/lock.hpp"

#include <format>

namespace repro {

Result<LockFile> read_lock(LuaHost& host, const Fs& fs, const ConfigRepository& repo) {
  LockFile lock;
  if (!fs.lstat(repo.lock).exists()) return lock;
  auto text = fs.read(repo.lock);
  if (!text) return std::unexpected(text.error());
  auto v = host.call("lock_parse", {*text, "repro.lock.lua"});
  if (!v) return std::unexpected(v.error());
  for (const auto& [iface, e] : (*v)["providers"].as_map()) {
    lock.providers[iface] =
        LockEntry{e.string_or("provider", ""), e.string_or("source", ""), e.string_or("decided_at", "")};
  }
  return lock;
}

bool merge_decisions(LockFile& lock, const std::map<std::string, ResolvedProvider>& decisions,
                     const std::string& now) {
  bool changed = false;
  for (const auto& [iface, p] : decisions) {
    if (!p.persist) continue;
    auto it = lock.providers.find(iface);
    if (it != lock.providers.end() && it->second.provider == p.provider &&
        it->second.source == p.source) {
      continue;
    }
    lock.providers[iface] = LockEntry{p.provider, p.source, now};
    changed = true;
  }
  return changed;
}

Result<std::string> render_lock(LuaHost& host, const LockFile& lock) {
  Map providers;
  for (const auto& [iface, e] : lock.providers) {
    providers.emplace(iface, Map{{"provider", e.provider}, {"source", e.source},
                                 {"decided_at", e.decided_at}});
  }
  auto v = host.call("lock_serialize", {Map{{"version", 1}, {"providers", std::move(providers)}}});
  if (!v) return std::unexpected(v.error());
  return v->as_string();
}

Status write_lock(LuaHost& host, Fs& fs, const ConfigRepository& repo, const LockFile& lock) {
  auto text = render_lock(host, lock);
  if (!text) return std::unexpected(text.error());
  const FileStat owner = fs.stat(repo.root);
  if (auto r = fs.write_atomic(repo.lock, *text, 0644); !r) return r;
  // Under sudo the lock keeps the repository owner's uid/gid.
  const FileStat now = fs.stat(repo.lock);
  if (owner.exists() && (now.uid != owner.uid || now.gid != owner.gid)) {
    (void)fs.chown(repo.lock, owner.uid, owner.gid);
  }
  return {};
}

}  // namespace repro
