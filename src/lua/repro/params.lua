-- Module parameters (FR-017, data-model.md "Param"): declaration
-- normalisation, type inference, validation, and resolution of overrides.
local err = require("repro.err")
local util = require("repro.util")

local M = {}

M.TYPES = { "string", "number", "boolean", "list", "map" }

local function value_type(v)
  local t = type(v)
  if t == "table" then return util.is_list(v) and "list" or "map" end
  return t
end

local function matches(ty, v)
  if ty == "list" then return type(v) == "table" and util.is_list(v) end
  if ty == "map" then
    if type(v) ~= "table" then return false end
    for k in pairs(v) do  -- order-insensitive: all-or-nothing check
      if type(k) ~= "string" then return false end
    end
    return true
  end
  return type(v) == ty
end

local function show(v)
  if type(v) == "string" then return string.format("%q", v) end
  if type(v) == "table" then return require("repro.canon").encode(v) end
  return tostring(v)
end

local function label(module, name)
  if module and name then return "param '" .. name .. "' of module " .. module end
  if name then return "param '" .. name .. "'" end
  return "rp.param"
end

local function allowed_text(one_of)
  local parts = {}
  for _, a in ipairs(one_of) do parts[#parts + 1] = show(a) end
  return table.concat(parts, ", ")
end

function M.check_value(module, name, decl, v)
  if not matches(decl.type, v) then
    err.raise("E_PARAM", label(module, name) .. " expects a " .. decl.type .. ", got " .. show(v)
      .. " (" .. value_type(v) .. ")", "pass a " .. decl.type .. " value")
  end
  if decl.one_of then
    for _, a in ipairs(decl.one_of) do
      if a == v then return v end
    end
    err.raise("E_PARAM", label(module, name) .. " got " .. show(v) .. ", which is not allowed",
      "allowed values: " .. allowed_text(decl.one_of))
  end
  return v
end

-- Accepts an rp.param{} table or a plain default value (shorthand).
function M.normalize(module, name, p)
  local builders = require("repro.builders")
  if builders.kind_of(p) ~= "param" then p = { default = p } end
  local decl = { description = p.description, one_of = p.one_of, default = p.default }
  decl.has_default = p.default ~= nil
  if p.type ~= nil then
    local ok = false
    for _, t in ipairs(M.TYPES) do
      if p.type == t then ok = true end
    end
    if not ok then
      err.raise("E_CONFIG", label(module, name) .. ": unknown type '" .. tostring(p.type) .. "'",
        util.did_you_mean(tostring(p.type), M.TYPES) or ("types: " .. table.concat(M.TYPES, ", ")))
    end
    decl.type = p.type
  elseif decl.has_default then
    decl.type = value_type(p.default)
  else
    err.raise("E_CONFIG", label(module, name) .. " needs a type or a default", "add type = \"string\"")
  end
  if decl.one_of ~= nil and not util.is_list(decl.one_of) then
    err.raise("E_CONFIG", label(module, name) .. ": one_of must be a list", "")
  end
  if decl.has_default then M.check_value(module, name, decl, decl.default) end
  return decl
end

function M.normalize_all(module, params)
  local out = {}
  for _, name in ipairs(util.sorted_keys_of_set(params or {})) do
    local p = params[name]
    if type(name) ~= "string" or not name:match("^[%a_][%w_]*$") then
      err.raise("E_CONFIG", "module " .. module .. ": invalid param name '" .. tostring(name) .. "'", "")
    end
    out[name] = M.normalize(module, name, p)
  end
  return out
end

-- Resolves values: override (profile) > declared default > error if required.
function M.resolve(module, decls, overrides)
  overrides = overrides or {}
  local names = util.sorted_keys_of_set(decls)
  for _, k in ipairs(util.sorted_keys_of_set(overrides)) do
    if decls[k] == nil then
      err.raise("E_PARAM", "module " .. module .. " has no param '" .. tostring(k) .. "'",
        util.did_you_mean(tostring(k), names) or ("declared params: " .. (#names > 0 and table.concat(names, ", ") or "none")))
    end
  end
  local values = {}
  for _, name in ipairs(names) do
    local decl = decls[name]
    local v = overrides[name]
    if v == nil then
      if not decl.has_default then
        err.raise("E_PARAM", label(module, name) .. " is required but has no value",
          "set it in the profile: { \"" .. module .. "\", params = { " .. name .. " = ... } }")
      end
      v = util.deep_copy(decl.default)
    end
    values[name] = M.check_value(module, name, decl, v)
  end
  return values
end

return M
