-- Backend facades (research R17, contracts/lua-api.md). `rp.packages` and
-- `rp.services` route through the host to whichever backend was selected
-- (native .so or repo-local Lua). Mutating calls are APPLY-only.
local host = require("repro.host")
local canon = require("repro.canon")
local err = require("repro.err")
local util = require("repro.util")

local M = {}

M.specs = {}  -- "<namespace>:<name>" -> rp.backend spec
M.current_module = nil  -- set by the loader for error context

local function context()
  return M.current_module and (" (module " .. M.current_module .. ")") or ""
end

local function require_apply(fn, hint)
  local phase = host.phase()
  if phase ~= "APPLY" then
    local when = phase == "EVAL" and "while configuration is being read" or "while checks are running"
    err.raise("E_DECLARATIVE", fn .. "() changes the system " .. when .. context(), hint)
  end
end

local function string_list(names, fn)
  if type(names) == "string" then names = { names } end
  if not util.is_list(names) then
    err.raise("E_CONFIG", fn .. "() expects a list of names", "pass { \"name\", ... }")
  end
  for _, n in ipairs(names) do
    if type(n) ~= "string" then err.raise("E_CONFIG", fn .. "() expects strings", "") end
  end
  return names
end

local function example(names)
  local quoted = {}
  for _, n in ipairs(names or {}) do quoted[#quoted + 1] = string.format("%q", tostring(n)) end
  return table.concat(quoted, ", ")
end

M.packages = {
  installed = function(names)
    return host.backend_call("packages", "installed", string_list(names, "rp.packages.installed"))
  end,
  install = function(names)
    names = string_list(names, "rp.packages.install")
    require_apply("rp.packages.install", "declare it instead: packages = { " .. example(names) .. " }")
    return host.backend_call("packages", "install", names)
  end,
  remove = function(names)
    names = string_list(names, "rp.packages.remove")
    require_apply("rp.packages.remove", "repro v1 never removes packages; drop them from the module instead")
    return host.backend_call("packages", "remove", names)
  end,
  refresh = function()
    require_apply("rp.packages.refresh", "use repro sync --upgrade for a full upgrade (a refresh alone is a partial upgrade)")
    return host.backend_call("packages", "refresh")
  end,
}

local function service_op(op)
  return function(unit, scope)
    if type(unit) ~= "string" then err.raise("E_CONFIG", "rp.services." .. op .. "() expects a unit name", "") end
    require_apply("rp.services." .. op,
      "declare it instead: services = { { unit = " .. string.format("%q", unit) .. ", state = \"enabled\" } }")
    return host.backend_call("services", op, unit, scope or "system")
  end
end

M.services = {
  state = function(unit, scope)
    if type(unit) ~= "string" then err.raise("E_CONFIG", "rp.services.state() expects a unit name", "") end
    return host.backend_call("services", "state", unit, scope or "system")
  end,
  enable = service_op("enable"),
  disable = service_op("disable"),
  start = service_op("start"),
  stop = service_op("stop"),
}

---------------------------------------------------------------------------
-- Repo-local Lua backends
---------------------------------------------------------------------------

function M.register(spec, path_info)
  local key = spec.namespace .. ":" .. spec.name
  M.specs[key] = spec
  spec._info = path_info
  return key
end

local function mem_store(spec)
  local path = host.backend_mem_path(spec.name, spec.privilege)
  local cache
  local function load()
    if cache then return cache end
    local text = host.read_file(path)
    cache = {}
    if text then
      local chunk = loadstring(text, "@" .. path)
      if chunk then
        setfenv(chunk, {})
        local ok, v = pcall(chunk)
        if ok and type(v) == "table" then cache = v end
      end
    end
    return cache
  end
  return {
    fetch = function(key, default)
      local v = load()[key]
      if v == nil then return default end
      return util.deep_copy(v)
    end,
    update = function(key, names, value)
      require_apply("ctx.mem.update", "only change backend state from install/enable operations")
      local mem = load()
      mem[key] = mem[key] or {}
      if type(names) == "string" then names = { names } end
      for _, n in ipairs(names) do mem[key][n] = value end
      host.write_file(path, "return " .. canon.encode(mem) .. "\n")
    end,
  }
end

local function backend_ctx(spec)
  return {
    mode = host.mode(),
    name = spec.name,
    mem = mem_store(spec),
    log = function(msg) host.log(tostring(msg)) end,
    exec = function(cmd)
      require_apply("ctx.exec", "run commands only from install/enable operations")
      return host.exec(cmd)
    end,
  }
end

local OPTIONAL = { refresh = true, remove = true, start = true, stop = true }

-- Called by the host (C++) for Lua backends.
function M.call(namespace, name, op, ...)
  local spec = M.specs[namespace .. ":" .. name]
  if not spec then err.raise("E_NO_BACKEND", "no Lua backend " .. namespace .. "/" .. name, "") end
  local fn = spec.ops[op]
  if fn == nil then
    if OPTIONAL[op] then return nil end
    err.raise("E_STEP", "backend " .. name .. " does not implement '" .. op .. "'", "add ops." .. op)
  end
  local ctx = backend_ctx(spec)
  if op == "probe" then return fn(ctx) and true or false end
  if op == "installed" then
    local names = ...
    local result = fn(ctx, names) or {}
    local out = {}
    for _, n in ipairs(names) do out[n] = result[n] and true or false end
    return out
  end
  if op == "state" then
    local r = fn(ctx, ...)
    if r == nil then return nil end
    return { enabled = r.enabled and true or false, active = r.active and true or false }
  end
  fn(ctx, ...)
  return true
end

return M
