-- with_params fragment merge (research R11): list fields concatenate and are
-- de-duplicated keeping the first occurrence; map fields merge, and the same
-- key with different values is an E_CONFLICT.
local canon = require("repd.canon")
local err = require("repd.err")
local util = require("repd.util")
local builders = require("repd.builders")

local M = {}

local LIST_FIELDS = { packages = true, files = true, services = true, users = true, udev = true, actions = true }
local MAP_FIELDS = { env = true, system_env = true }

local function identity(v)
  return canon.encode(v, { on_function = function(f) return tostring(f) end })
end

local function concat_dedup(a, b)
  local out, seen = {}, {}
  for _, list in ipairs({ a or {}, b or {} }) do
    for _, item in ipairs(list) do
      local id = identity(item)
      if not seen[id] then
        seen[id] = true
        out[#out + 1] = item
      end
    end
  end
  return out
end

local function merge_map(module, field, a, b)
  local out = util.shallow_copy(a or {})
  for _, k in ipairs(util.sorted_keys_of_set(b or {})) do
    local v = b[k]
    if out[k] ~= nil and identity(out[k]) ~= identity(v) then
      err.raise("E_CONFLICT", "module " .. module .. ": " .. field .. "." .. tostring(k)
        .. " is set to both " .. identity(out[k]) .. " and " .. identity(v),
        "set " .. tostring(k) .. " in only one place")
    end
    out[k] = v
  end
  return out
end

local function merge_locale(module, a, b)
  if a == nil then return b end
  if b == nil then return a end
  if a.default and b.default and a.default ~= b.default then
    err.raise("E_CONFLICT", "module " .. module .. ": locale.default is both '" .. a.default
      .. "' and '" .. b.default .. "'", "declare one default locale")
  end
  return { locales = concat_dedup(a.locales, b.locales), default = a.default or b.default }
end

function M.merge(module, base, fragment)
  local out = util.shallow_copy(base)
  if fragment == nil then return out end
  if type(fragment) ~= "table" then
    err.raise("E_CONFIG", "module " .. module .. ": with_params must return a table", "return { packages = { ... } }")
  end
  local content = {}
  for _, f in ipairs(builders.CONTENT_FIELDS) do content[f] = true end
  for _, k in ipairs(util.sorted_keys_of_set(fragment)) do
    if not content[k] then
      local known = false
      for _, f in ipairs(builders.MODULE_FIELDS) do
        if f == k then known = true end
      end
      err.raise("E_CONFIG", "module " .. module .. ": with_params returned field '" .. tostring(k) .. "'"
        .. (known and ", which is not a content field" or ""),
        known and ("with_params may only return " .. table.concat(builders.CONTENT_FIELDS, ", "))
          or (util.did_you_mean(tostring(k), builders.CONTENT_FIELDS) or ""))
    end
  end
  for _, k in ipairs(util.sorted_keys_of_set(fragment)) do
    local v = fragment[k]
    if LIST_FIELDS[k] then
      out[k] = concat_dedup(out[k], v)
    elseif MAP_FIELDS[k] then
      out[k] = merge_map(module, k, out[k], v)
    elseif k == "locale" then
      out[k] = merge_locale(module, out[k], v)
    end
  end
  return out
end

return M
