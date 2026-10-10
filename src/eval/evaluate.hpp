// EVAL phase driver (research R9, R11): loads repd.lua, repo-local Lua
// backends and every module file into fresh sandboxes, validates references
// between them, and resolves selected modules' parameterised declarations.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <repd/config.hpp>

#include "eval/declaration.hpp"
#include "eval/discovery.hpp"
#include "eval/host.hpp"
#include "util/errors.hpp"

namespace repd {

class Evaluator {
 public:
  Evaluator(LuaHost& host, const Fs& fs, ConfigRepository repo);

  const ConfigRepository& repo() const { return repo_; }

  Result<RootConfig> load_root();
  Result<std::vector<LuaBackendSpec>> load_lua_backends();
  Result<std::map<std::string, ModuleMeta>> load_modules();
  // Cross-references: provides -> interface, requires -> module/interface,
  // profile use -> module. Unknown names fail with E_UNKNOWN + suggestion.
  // Cross-references; also splits a trailing "+" off profile use entries.
  static Status validate(RootConfig& root, const std::map<std::string, ModuleMeta>& modules);

  // Params (override map from a profile `use` entry) + with_params + src paths.
  Result<ResolvedModule> resolve(const std::string& name, const Value& param_overrides);

 private:
  LuaHost& host_;
  const Fs& fs_;
  ConfigRepository repo_;
  bool configured_ = false;
  Status configure();
};

}  // namespace repd
