-- Bootstrap entry point (module "repro"). Builds the `rp` facade and exposes
-- the functions the C++ host drives: load the root config, plugins and
-- modules (EVAL), resolve parameterised declarations, run actions, and
-- canonical encoding for fingerprints / lock / show.
local host = require("repro.host")
local canon = require("repro.canon")
local err = require("repro.err")
local util = require("repro.util")
local sandbox = require("repro.sandbox")
local builders = require("repro.builders")
local params = require("repro.params")
local merge = require("repro.merge")
local lock = require("repro.lock")
local backend = require("repro.backend")

local M = {}

M.API_VERSION = 1

local function sys_table()
  return {
    hostname = function() return host.sys_hostname() end,
    user = function() return host.sys_user() end,
    home = function() return host.sys_home() end,
    arch = function() return host.sys_arch() end,
    os_release = function() return host.sys_os_release() end,
  }
end

local rp_raw = {
  api_version = M.API_VERSION,
  module = builders.module,
  interface = builders.interface,
  param = builders.param,
  action = builders.action,
  profile = builders.profile,
  option = builders.option,
  pin = builders.pin,
  backend = builders.backend,
  fs = {
    exists = function(p) return host.fs_exists(p) end,
    read = function(p) return host.fs_read(p) end,
    stat = function(p) return host.fs_stat(p) end,
    list = function(p) return host.fs_list(p) end,
  },
  sys = sys_table(),
  packages = backend.packages,
  services = backend.services,
}

M.rp_raw = rp_raw
M.rp = sandbox.read_only(rp_raw, "rp")

-- Sorted list of every public rp symbol (docs registry test, research R18).
function M.api_registry()
  local out = {}
  for k, v in pairs(rp_raw) do  -- order-insensitive: sorted below
    if type(v) == "table" then
      for k2 in pairs(v) do out[#out + 1] = "rp." .. k .. "." .. k2 end  -- order-insensitive
    else
      out[#out + 1] = "rp." .. k
    end
  end
  table.sort(out)
  return out
end

local S = {
  config_root = nil,
  modules = {},  -- name -> { spec, kind, path, rel, dir, sha, params }
  resolved = {}, -- name -> { decl, values }
}

function M.configure(cfg)
  S.config_root = cfg.config_root
  S.modules, S.resolved = {}, {}
  return true
end

local function fn_id(f)
  local info = debug.getinfo(f, "S")
  return "<function " .. info.short_src .. ":" .. info.linedefined .. ">"
end

-- Plain data copy: functions become their source-position ids.
local function to_plain(v, seen)
  if type(v) == "function" then return fn_id(v) end
  if type(v) ~= "table" then return v end
  seen = seen or {}
  if seen[v] then err.raise("E_CONFIG", "declaration contains a cycle", "") end
  seen[v] = true
  local out = {}
  for k, x in pairs(v) do out[k] = to_plain(x, seen) end  -- order-insensitive: copy
  seen[v] = nil
  return out
end

local function run_chunk(chunk, context)
  local ok, result = pcall(chunk)
  if not ok then error(err.wrap(result, "E_CONFIG", context), 0) end
  return result
end

---------------------------------------------------------------------------
-- EVAL: root config, plugins, modules
---------------------------------------------------------------------------

function M.load_root(path, rel)
  local env = sandbox.new_env(M.rp, rp_raw)
  local chunk = sandbox.load_file(path, rel, env)
  builders.collector = { options = {}, pins = {}, profiles = {} }
  local ok, e = pcall(run_chunk, chunk, rel)
  local collected = builders.collector
  builders.collector = nil
  if not ok then error(e, 0) end
  local defaults = {}
  for _, p in ipairs(collected.profiles) do
    if p.default then defaults[#defaults + 1] = p.name end
  end
  if #defaults > 1 then
    err.raise("E_CONFIG", "profiles " .. table.concat(defaults, " and ") .. " are both marked default",
      "mark at most one profile with default = true")
  end
  return collected
end

function M.load_backend(path, rel)
  local env = sandbox.new_env(M.rp, rp_raw)
  local chunk, source = sandbox.load_file(path, rel, env)
  local spec = run_chunk(chunk, rel)
  if builders.kind_of(spec) ~= "backend" then
    err.raise("E_CONFIG", rel .. " must return rp.backend { ... }", "see docs/lua-api.md, Repo-local backends")
  end
  backend.register(spec, { path = path, rel = rel })
  return {
    namespace = spec.namespace,
    name = spec.name,
    privilege = spec.privilege,
    version = "lua:" .. host.sha256(source):sub(1, 8),
    path = rel,
    upgrade = spec.ops.upgrade ~= nil,
    upgrade_packages = spec.ops.upgrade_packages ~= nil,
  }
end

function M.backend_call(namespace, name, op, ...)
  return backend.call(namespace, name, op, ...)
end

-- requires entries split into the modules synced along and the optional ones.
local function split_requires(list)
  local hard, optional = {}, {}
  for _, r in ipairs(list or {}) do
    if type(r) == "table" then
      if r.optional then optional[#optional + 1] = r[1] else hard[#hard + 1] = r[1] end
    else
      hard[#hard + 1] = r
    end
  end
  return hard, optional
end

function M.load_module(name, path, rel)
  local env = sandbox.new_env(M.rp, rp_raw)
  backend.current_module = name
  local ok, result = pcall(function()
    local chunk, source = sandbox.load_file(path, rel, env)
    local spec = run_chunk(chunk, "module " .. name .. ", " .. rel)
    return { spec = spec, source = source }
  end)
  backend.current_module = nil
  if not ok then error(err.wrap(result, "E_CONFIG", "module " .. name .. ", " .. rel), 0) end
  local spec = result.spec
  local kind = builders.kind_of(spec)
  if kind ~= "module" and kind ~= "interface" then
    err.raise("E_CONFIG", rel .. " must return rp.module { ... } or rp.interface { ... }",
      "end the file with: return rp.module { ... }")
  end
  if spec.name ~= nil and spec.name ~= name then
    err.raise("E_NAME", rel .. " declares name '" .. spec.name .. "' but its path makes it '" .. name .. "'",
      "remove the name field or rename the file")
  end
  local ok2, decls = pcall(params.normalize_all, name, spec.params)
  if not ok2 then error(err.wrap(decls, "E_CONFIG", "module " .. name .. ", " .. rel), 0) end
  S.modules[name] = {
    spec = spec, kind = kind, path = path, rel = rel, dir = util.dirname(path),
    sha = host.sha256(result.source), params = decls,
  }
  local meta_params = {}
  for pname, d in pairs(decls) do  -- order-insensitive: builds a map
    meta_params[pname] = {
      type = d.type, default = d.default, has_default = d.has_default,
      one_of = d.one_of, description = d.description,
    }
  end
  return {
    name = name,
    kind = kind,
    description = spec.description or "",
    provides = spec.provides,
    priority = spec.priority or 100,
    requires = (split_requires(spec.requires)),
    optional = select(2, split_requires(spec.requires)),
    integrates = spec.integrates or {},
    auto_sync = spec.auto_sync ~= false,
    params = meta_params,
    has_with_params = spec.with_params ~= nil,
    source = rel,
    source_sha256 = S.modules[name].sha,
  }
end

---------------------------------------------------------------------------
-- Resolution of a module's declaration (params + with_params + src paths)
---------------------------------------------------------------------------

-- Sources live next to their module: modules/<name>/files/<src>. Only
-- directory modules (modules/<name>/init.lua) can carry files.
local function resolve_src(m, name, src, what)
  if not m.path:match("/init%.lua$") then
    err.raise("E_CONFIG", "module " .. name .. ": " .. what .. " uses src, but " .. m.rel
      .. " is a single-file module",
      "move it to modules/" .. name .. "/init.lua and put " .. src .. " in modules/" .. name .. "/files/")
  end
  if src:sub(1, 1) == "/" or src:sub(1, 1) == "~" then
    err.raise("E_CONFIG", "module " .. name .. ": " .. what .. ".src must be relative to the module's files/ (got "
      .. src .. ")", "put the file in modules/" .. name .. "/files/ and write src = \"name\"")
  end
  local base = m.dir .. "/files"
  local abs = util.normalize_path(base .. "/" .. src)
  if not abs or not util.starts_with_path(abs, base) then
    err.raise("E_CONFIG", "module " .. name .. ": " .. what .. ".src " .. src
      .. " points outside modules/" .. name .. "/files/",
      "keep a module's sources in its own files/ directory")
  end
  return abs:sub(#S.config_root + 2)
end

function M.resolve_module(name, overrides)
  local m = S.modules[name]
  if not m then err.raise("E_UNKNOWN", "unknown module " .. tostring(name), "") end
  local ctx = "module " .. name .. ", " .. m.rel
  backend.current_module = name
  local ok, result = pcall(function()
    local values = params.resolve(name, m.params, overrides)
    local decl = {}
    for _, f in ipairs(builders.CONTENT_FIELDS) do
      if m.spec[f] ~= nil then decl[f] = util.deep_copy(m.spec[f]) end
    end
    -- deep_copy drops builder tags; actions keep their original (tagged) tables.
    decl.actions = m.spec.actions
    if m.spec.with_params then
      -- Params passed straight through ({ locales = p.locales }) are proxies.
      local fragment = sandbox.plain(
        m.spec.with_params(sandbox.read_only(util.deep_copy(values), "params"), M.rp.sys))
      decl = merge.merge(name, decl, fragment)
      builders.validate_content(decl)
    end
    for i, f in ipairs(decl.files or {}) do
      if f.src then f.src_path = resolve_src(m, name, f.src, "files[" .. i .. "]") end
    end
    for i, r in ipairs(decl.udev or {}) do
      if r.src then r.src_path = resolve_src(m, name, r.src, "udev[" .. i .. "]") end
    end
    return { decl = decl, values = values }
  end)
  backend.current_module = nil
  if not ok then error(err.wrap(result, "E_CONFIG", ctx), 0) end
  S.resolved[name] = result
  local spec, decl = m.spec, result.decl
  local plain = to_plain(decl)
  plain.name = name
  plain.kind = m.kind
  plain.description = spec.description or ""
  plain.provides = spec.provides
  plain.priority = spec.priority or 100
  plain.requires, plain.optional = split_requires(spec.requires)
  plain.integrates = util.deep_copy(spec.integrates or {})
  plain.auto_sync = spec.auto_sync ~= false
  plain.params = util.deep_copy(result.values)
  plain.with_params = spec.with_params and fn_id(spec.with_params) or nil
  plain.source = m.rel
  plain.source_sha256 = m.sha
  return plain
end

---------------------------------------------------------------------------
-- Actions (CHECK / APPLY phases)
---------------------------------------------------------------------------

local function find_action(module, name)
  local r = S.resolved[module]
  for _, a in ipairs((r and r.decl.actions) or {}) do
    if a.name == name then return a, r end
  end
  err.raise("E_UNKNOWN", "module " .. module .. " has no action '" .. name .. "'", "")
end

function M.action(module, name, op, info)
  local a, r = find_action(module, name)
  local ctx = {
    mode = info.mode,
    home = info.home,
    user = info.user,
    params = sandbox.read_only(util.deep_copy(r.values), "ctx.params"),
    providers = sandbox.read_only(util.deep_copy(info.providers or {}), "ctx.providers"),
    fs = M.rp.fs,
    log = function(msg) host.log(tostring(msg)) end,
    exec = function(cmd)
      if host.phase() ~= "APPLY" then
        err.raise("E_DECLARATIVE", "ctx.exec() called from the check of action '" .. name
          .. "' (module " .. module .. ")", "checks must not change anything; inspect state with ctx.fs")
      end
      if type(cmd) ~= "string" then err.raise("E_CONFIG", "ctx.exec expects a command string", "") end
      return host.exec(cmd)
    end,
  }
  local fn = op == "check" and a.check or a.run
  local ok, result = pcall(fn, ctx)
  if not ok then
    if err.is(result) then error(result, 0) end
    err.raise("E_STEP", "action '" .. name .. "' (" .. op .. ") failed: " .. tostring(result),
      "fix the action in module " .. module)
  end
  if op == "check" then return result and true or false end
  return true
end

---------------------------------------------------------------------------
-- Canonical encoding services
---------------------------------------------------------------------------

function M.fingerprint(record)
  return host.sha256(canon.encode(record))
end

function M.encode(v)
  return canon.encode(v)
end

function M.pretty(v)
  return canon.pretty(v)
end

function M.lock_parse(text, file)
  return lock.parse(text, file)
end

function M.lock_serialize(l)
  return lock.serialize(l)
end

return M
