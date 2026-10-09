// repro.lock.lua (contracts/lockfile.md): parsed and serialised by the Lua
// bootstrap (lock.lua); this wrapper reads, merges decisions and writes it
// atomically, keeping the repository owner's uid/gid under sudo.
#pragma once

#include <map>
#include <string>

#include <repro/config.hpp>

#include "eval/host.hpp"
#include "plan/resolver.hpp"
#include "platform/fs.hpp"
#include "util/errors.hpp"

namespace repro {

struct LockFile {
  std::map<std::string, LockEntry> providers;
};

Result<LockFile> read_lock(LuaHost& host, const Fs& fs, const ConfigRepository& repo);

// Applies persisted decisions (cli/prompt/only-option). decided_at changes only
// when provider or source changes. Returns true if the lock content changed.
bool merge_decisions(LockFile& lock, const std::map<std::string, ResolvedProvider>& decisions,
                     const std::string& now);

Result<std::string> render_lock(LuaHost& host, const LockFile& lock);
Status write_lock(LuaHost& host, Fs& fs, const ConfigRepository& repo, const LockFile& lock);

}  // namespace repro
