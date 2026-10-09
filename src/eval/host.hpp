// LuaJIT host (research R4, R9): owns the lua_State, loads the embedded
// bootstrap through a custom package searcher, exposes host services to it
// as the private module "repro.host", and tracks the evaluation phase.
#pragma once

#include <lua.hpp>

#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "eval/embedded.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"
#include "util/errors.hpp"
#include "util/value.hpp"

namespace repro {

enum class Phase { Eval, Check, Apply };
std::string_view to_string(Phase p);

// Backend calls from Lua (rp.packages.* / rp.services.*): namespace, op, args.
using BackendDispatch =
    std::function<Result<Value>(const std::string& ns, const std::string& op, const List& args)>;

class LuaHost {
 public:
  LuaHost(const Paths& paths, Fs& fs, Mode mode, std::span<const EmbeddedFile> extra = {});
  ~LuaHost();
  LuaHost(const LuaHost&) = delete;
  LuaHost& operator=(const LuaHost&) = delete;

  lua_State* state() { return L_; }
  const Paths& paths() const { return paths_; }
  Fs& fs() { return fs_; }
  Mode mode() const { return mode_; }

  Phase phase() const { return phase_; }
  void set_phase(Phase p) { phase_ = p; }

  void set_backend_dispatch(BackendDispatch d) { backend_dispatch_ = std::move(d); }
  const BackendDispatch& backend_dispatch() const { return backend_dispatch_; }

  // ctx.log / ctx.exec output is appended here while a step runs.
  void set_log_sink(std::string* sink) { log_sink_ = sink; }
  void log(std::string_view msg);

  // Calls require(module)[fn](args...) in protected mode; returns the first result.
  Result<Value> call(std::string_view module, std::string_view fn, const List& args = {});
  // Shorthand for the bootstrap entry module "repro".
  Result<Value> call(std::string_view fn, const List& args = {}) { return call("repro", fn, args); }

  std::span<const EmbeddedFile> extra_files() const { return extra_; }

 private:
  lua_State* L_ = nullptr;
  const Paths& paths_;
  Fs& fs_;
  Mode mode_;
  Phase phase_ = Phase::Eval;
  BackendDispatch backend_dispatch_;
  std::string* log_sink_ = nullptr;
  std::span<const EmbeddedFile> extra_;
};

// Sets a phase for the lifetime of the scope.
class PhaseScope {
 public:
  PhaseScope(LuaHost& host, Phase p) : host_(host), saved_(host.phase()) { host.set_phase(p); }
  ~PhaseScope() { host_.set_phase(saved_); }
  PhaseScope(const PhaseScope&) = delete;
  PhaseScope& operator=(const PhaseScope&) = delete;

 private:
  LuaHost& host_;
  Phase saved_;
};

std::string utc_now();

}  // namespace repro
