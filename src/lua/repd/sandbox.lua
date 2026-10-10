-- Evaluation sandbox (research R9, FR-018). Every config file gets a fresh
-- environment holding only `rp` and side-effect-free standard functions.
-- Ambient state (io, os, require, debug, ffi, jit, math.random) is absent.
local err = require("repd.err")
local util = require("repd.util")

local M = {}

M.SAFE_GLOBALS = {
  "select", "pairs", "ipairs", "next", "type", "tostring", "tonumber", "error",
  "assert", "pcall", "unpack", "setmetatable", "getmetatable", "rawget", "rawset",
  "rawequal",
}

M.FORBIDDEN = {
  io = true, os = true, require = true, debug = true, ffi = true, jit = true,
  package = true, load = true, loadstring = true, loadfile = true, dofile = true,
  collectgarbage = true, module = false, xpcall = true, newproxy = true, getfenv = true,
  setfenv = true, coroutine = true, bit = true, print = true, gcinfo = true,
}

local function copy_lib(lib, exclude)
  local out = {}
  for k, v in pairs(lib) do  -- order-insensitive: copy
    if not (exclude and exclude[k]) then out[k] = v end
  end
  return out
end

-- Proxy -> the table it shows, and its name (weak, so proxies die with their tables).
local sources = setmetatable({}, { __mode = "k" })
local labels = setmetatable({}, { __mode = "k" })

local function read_only_error(name, k)
  err.raise("E_CONFIG", name .. " is read-only (attempt to set '" .. tostring(k) .. "')",
    "keep your own values in local variables")
end

-- Read-only proxy over a table tree; writes raise E_CONFIG. LuaJIT runs with
-- plain 5.1 semantics (no __len, __pairs or __ipairs), so the list part is
-- copied into the proxy itself: #, ipairs, table.concat and unpack see it
-- natively. Map keys stay behind __index/__newindex, and the sandbox's pairs,
-- next and table functions know about proxies (new_env).
function M.read_only(t, label)
  local proxies = {}
  local function wrap(tbl, name)
    if proxies[tbl] then return proxies[tbl] end
    local function view(v, k)
      if type(v) == "table" and getmetatable(v) == nil then return wrap(v, name .. "." .. tostring(k)) end
      return v
    end
    local p = {}
    proxies[tbl] = p
    sources[p] = tbl
    labels[p] = name
    for i, v in ipairs(tbl) do rawset(p, i, view(v, i)) end
    setmetatable(p, {
      __index = function(_, k) return view(tbl[k], k) end,
      __newindex = function(_, k) read_only_error(name, k) end,
      __metatable = false,
    })
    return p
  end
  return wrap(t, label)
end

function M.is_read_only(t) return sources[t] ~= nil end

-- Replaces every proxy in a value leaving user code (with_params fragments) by
-- a plain copy of its contents: validators and C++ iterate raw. Tables the
-- user built keep their identity (builders tag rp.action values by identity),
-- with proxy-valued fields swapped in place.
function M.plain(v, seen)
  if type(v) ~= "table" then return v end
  seen = seen or {}
  if seen[v] ~= nil then return seen[v] end
  local src = sources[v]
  if not src then
    seen[v] = v
    for k, x in pairs(v) do  -- order-insensitive: in-place replacement
      if type(x) == "table" then
        local px = M.plain(x, seen)
        if px ~= x then rawset(v, k, px) end
      end
    end
    return v
  end
  local out = {}
  seen[v] = out
  for k, x in pairs(src) do out[k] = M.plain(x, seen) end  -- order-insensitive: copy
  return out
end

-- pairs/next/table.* for sandboxed code: proxies iterate their source and
-- refuse in-place edits (rawset in table.insert would bypass __newindex).
local function proxy_next(t, k)
  local src = sources[t]
  if not src then return next(t, k) end
  local nk = next(src, k)
  if nk == nil then return nil end
  return nk, t[nk]
end

local function proxy_iter(t)  -- the sandbox's pairs(): ordering is the caller's concern
  if sources[t] then return proxy_next, t, nil end
  return next, t, nil
end

local function refuse_proxy(fn, fname)
  return function(t, ...)
    if sources[t] then
      err.raise("E_CONFIG", labels[t] .. " is read-only (table." .. fname .. " would change it)",
        "copy it first: local list = { unpack(" .. labels[t] .. ") }")
    end
    return fn(t, ...)
  end
end

local function rp_names(rp)
  local names = {}
  for k, v in pairs(rp) do  -- order-insensitive: sorted below
    if type(k) == "string" and k:sub(1, 1) ~= "_" then
      names[#names + 1] = k
      if type(v) == "table" then
        for k2 in pairs(v) do  -- order-insensitive: sorted below
          if type(k2) == "string" then names[#names + 1] = k2 end
        end
      end
    end
  end
  table.sort(names)
  return names
end

local function undefined_global(rp_raw, name)
  local where = err.where(3)
  local msg = "undefined global '" .. tostring(name) .. "'" .. (where and (" at " .. where) or "")
  local hint
  if rp_raw[name] ~= nil then
    hint = "did you mean rp." .. name .. "?"
  elseif M.FORBIDDEN[name] then
    hint = "'" .. name .. "' is not available while configuration is read; "
      .. "use rp.fs / rp.sys queries, or an action for imperative work"
  else
    local c = util.closest(name, rp_names(rp_raw))
    if c then
      hint = "did you mean rp." .. c .. "?"
    else
      c = util.closest(name, M.SAFE_GLOBALS)
      hint = c and ("did you mean '" .. c .. "'?") or "declare it as a local first"
    end
  end
  err.raise("E_CONFIG", msg, hint)
end

-- rp_facade: the read-only `rp` proxy; rp_raw: the underlying table (for hints).
function M.new_env(rp_facade, rp_raw)
  rp_raw = rp_raw or require("repd").rp_raw
  local env = {}
  for _, k in ipairs(M.SAFE_GLOBALS) do env[k] = _G[k] end
  env.pairs = proxy_iter
  env.next = proxy_next
  env.string = copy_lib(string)
  env.table = copy_lib(table)
  for _, f in ipairs({ "insert", "remove", "sort" }) do
    env.table[f] = refuse_proxy(table[f], f)
  end
  env.math = copy_lib(math, { random = true, randomseed = true })
  env.rp = rp_facade
  return setmetatable(env, {
    __index = function(_, k) undefined_global(rp_raw, k) end,
  })
end

-- Loads a file as a chunk named `@<display>` and binds it to `env`.
function M.load_file(path, display, env)
  local f, open_err = io.open(path, "rb")
  if not f then
    err.raise("E_CONFIG", "cannot read " .. display .. ": " .. tostring(open_err), "check the file exists and is readable")
  end
  local source = f:read("*a")
  f:close()
  if source:sub(1, 1) == "\27" then
    err.raise("E_CONFIG", display .. " is precompiled bytecode", "configuration must be Lua source")
  end
  local chunk, syntax = loadstring(source, "@" .. display)
  if not chunk then
    err.raise("E_CONFIG", "syntax error: " .. tostring(syntax), "fix the Lua syntax at the reported line")
  end
  setfenv(chunk, env)
  return chunk, source
end

return M
