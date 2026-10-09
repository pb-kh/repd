-- Canonical, byte-stable serialisation (research R8). One serializer for
-- fingerprints (encode), repro.lock.lua and `repro show` (pretty).
-- Keys are sorted by type name, then by value; integers print without a
-- fraction; strings use %q; functions/userdata/cdata and cycles are rejected.
local M = {}

local type, tostring, floor, format = type, tostring, math.floor, string.format
local huge = math.huge

local TYPE_RANK = { boolean = 1, number = 2, string = 3 }

local KEYWORDS = {}
for w in ([[and break do else elseif end false for function if in local nil not or
  repeat return then true until while goto]]):gmatch("%S+") do KEYWORDS[w] = true end

local function key_less(a, b)
  local ta, tb = type(a), type(b)
  if ta ~= tb then return (TYPE_RANK[ta] or 9) < (TYPE_RANK[tb] or 9) end
  if ta == "boolean" then return (not a) and b end
  return a < b
end

function M.sorted_keys(t)
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = k end  -- order-insensitive: sorted below
  table.sort(keys, key_less)
  return keys
end

function M.sorted_pairs(t)
  local keys, i = M.sorted_keys(t), 0
  return function()
    i = i + 1
    local k = keys[i]
    if k ~= nil then return k, t[k] end
  end
end

local function number_text(n)
  if n ~= n or n == huge or n == -huge then
    error("canon: number is not finite", 0)
  end
  if n == floor(n) and n > -2 ^ 53 and n < 2 ^ 53 then
    if n == 0 then return "0" end
    return format("%d", n)
  end
  return format("%.17g", n)
end

-- Returns n if t is a proper sequence 1..n (n may be 0), else nil.
local function sequence_length(t)
  local n = 0
  for k in pairs(t) do  -- order-insensitive: all-or-nothing check
    if type(k) ~= "number" or k < 1 or k ~= floor(k) then return nil end
    n = n + 1
  end
  for i = 1, n do
    if t[i] == nil then return nil end
  end
  return n
end

local function key_text(k)
  if type(k) == "string" and k:match("^[%a_][%w_]*$") and not KEYWORDS[k] then return k end
  if type(k) == "string" then return "[" .. format("%q", k) .. "]" end
  if type(k) == "number" then return "[" .. number_text(k) .. "]" end
  if type(k) == "boolean" then return "[" .. tostring(k) .. "]" end
  error("canon: unsupported key type " .. type(k), 0)
end

local function scalar_text(v, opts, path)
  local tv = type(v)
  if tv == "nil" then return "nil" end
  if tv == "boolean" then return tostring(v) end
  if tv == "number" then return number_text(v) end
  if tv == "string" then return (format("%q", v):gsub("\\\n", "\\n")) end
  if tv == "function" and opts.on_function then
    return scalar_text(opts.on_function(v), {}, path)
  end
  error("canon: cannot encode a " .. tv .. " at " .. path, 0)
end

local function encode(v, opts, active, path, out)
  if type(v) ~= "table" then
    out[#out + 1] = scalar_text(v, opts, path)
    return
  end
  if active[v] then error("canon: cycle detected at " .. path, 0) end
  active[v] = true
  local n = sequence_length(v)
  out[#out + 1] = "{"
  if n then
    for i = 1, n do
      if i > 1 then out[#out + 1] = "," end
      encode(v[i], opts, active, path .. "[" .. i .. "]", out)
    end
  else
    local first = true
    for _, k in ipairs(M.sorted_keys(v)) do
      if not first then out[#out + 1] = "," end
      first = false
      out[#out + 1] = key_text(k) .. "="
      encode(v[k], opts, active, path .. "." .. tostring(k), out)
    end
  end
  out[#out + 1] = "}"
  active[v] = nil
end

-- opts.on_function(f) -> replacement value (used for fingerprints/show).
function M.encode(v, opts)
  local out = {}
  encode(v, opts or {}, {}, "value", out)
  return table.concat(out)
end

local function pretty(v, opts, active, path, indent, out)
  if type(v) ~= "table" then
    out[#out + 1] = scalar_text(v, opts, path)
    return
  end
  if active[v] then error("canon: cycle detected at " .. path, 0) end
  if next(v) == nil then
    out[#out + 1] = "{}"
    return
  end
  active[v] = true
  local inner = indent .. "  "
  out[#out + 1] = "{\n"
  local n = sequence_length(v)
  if n then
    for i = 1, n do
      out[#out + 1] = inner
      pretty(v[i], opts, active, path .. "[" .. i .. "]", inner, out)
      out[#out + 1] = ",\n"
    end
  else
    for _, k in ipairs(M.sorted_keys(v)) do
      out[#out + 1] = inner .. key_text(k) .. " = "
      pretty(v[k], opts, active, path .. "." .. tostring(k), inner, out)
      out[#out + 1] = ",\n"
    end
  end
  out[#out + 1] = indent .. "}"
  active[v] = nil
end

function M.pretty(v, opts)
  local out = {}
  pretty(v, opts or {}, {}, "value", "", out)
  return table.concat(out)
end

return M
