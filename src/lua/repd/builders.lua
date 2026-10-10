-- Declaration builders (contracts/lua-api.md). Builders only *record*: they
-- validate shape, tag the table with its kind, and return it. Nothing here
-- touches the machine.
local err = require("repd.err")
local util = require("repd.util")

local M = {}

local kinds = setmetatable({}, { __mode = "k" })

function M.kind_of(t)
  return type(t) == "table" and kinds[t] or nil
end

local function tag(t, kind)
  kinds[t] = kind
  return t
end

M.MODULE_FIELDS = {
  "name", "description", "provides", "priority", "requires", "integrates", "auto_sync", "params",
  "with_params", "packages", "files", "env", "system_env", "services", "users", "locale", "udev", "actions",
}
M.CONTENT_FIELDS = {
  "packages", "files", "env", "system_env", "services", "users", "locale", "udev", "actions",
}
local FILE_FIELDS = { "dest", "src", "content", "action", "mode", "scope", "adopt" }
local SERVICE_FIELDS = { "unit", "scope", "state", "active" }
local USER_FIELDS = { "name", "shell", "groups", "state" }
local LOCALE_FIELDS = { "locales", "default" }
local UDEV_FIELDS = { "name", "src", "content" }
local ACTION_FIELDS = { "name", "mode", "check", "run" }
local PARAM_FIELDS = { "default", "type", "one_of", "description" }
local PROFILE_FIELDS = { "name", "description", "default", "use", "providers" }
local BACKEND_FIELDS = { "namespace", "name", "privilege", "ops" }
M.OPTION_KEYS = { "packages.default_backend", "services.backend", "env.targets" }
M.ENV_TARGETS = { "sh", "fish", "environment.d" }

M.MODULE_NAME_PATTERN = "^[a-z0-9_+-]+$"

function M.valid_module_name(name)
  if type(name) ~= "string" or name == "" then return false end
  for part in (name .. "/"):gmatch("([^/]*)/") do
    if not part:match(M.MODULE_NAME_PATTERN) then return false end
  end
  return true
end

local function set_of(list)
  local s = {}
  for _, v in ipairs(list) do s[v] = true end
  return s
end

local function check_fields(t, allowed, what)
  local allowed_set = set_of(allowed)
  for _, k in ipairs(util.sorted_keys_of_set(t)) do
    if not allowed_set[k] then
      local hint = util.did_you_mean(k, allowed)
        or ("valid fields: " .. table.concat(allowed, ", "))
      err.raise("E_CONFIG", "unknown field '" .. tostring(k) .. "' in " .. what, hint)
    end
  end
end

local function expect_type(v, ty, what, hint)
  if type(v) ~= ty then
    err.raise("E_CONFIG", what .. " must be a " .. ty .. " (got " .. type(v) .. ")", hint or "")
  end
end

local function expect_list(v, what)
  if v == nil then return {} end
  if not util.is_list(v) then
    err.raise("E_CONFIG", what .. " must be a list", "write it as { item1, item2 }")
  end
  return v
end

local function expect_string_list(v, what)
  for i, s in ipairs(expect_list(v, what)) do
    if type(s) ~= "string" or s == "" then
      err.raise("E_CONFIG", what .. "[" .. i .. "] must be a non-empty string", "")
    end
  end
  return v or {}
end

local function expect_one_of(v, allowed, what)
  for _, a in ipairs(allowed) do
    if v == a then return v end
  end
  err.raise("E_CONFIG", what .. " must be one of " .. table.concat(allowed, ", ")
    .. " (got " .. tostring(v) .. ")",
    util.did_you_mean(tostring(v), allowed) or ("use one of: " .. table.concat(allowed, ", ")))
end

---------------------------------------------------------------------------
-- Content validation (shared by rp.module and merged with_params fragments)
---------------------------------------------------------------------------

local VARNAME = "^[%a_][%w_]*$"

local function validate_env(env, what)
  if env == nil then return end
  expect_type(env, "table", what)
  for _, k in ipairs(util.sorted_keys_of_set(env)) do
    local v = env[k]
    if type(k) ~= "string" or not k:match(VARNAME) then
      err.raise("E_CONFIG", what .. ": invalid variable name '" .. tostring(k) .. "'",
        "variable names match [A-Za-z_][A-Za-z0-9_]*")
    end
    if type(v) == "number" then
      env[k] = tostring(v)
    elseif type(v) ~= "string" then
      err.raise("E_CONFIG", what .. "." .. k .. " must be a string", "")
    end
  end
end

local function validate_src_content(e, what)
  if (e.src == nil) == (e.content == nil) then
    err.raise("E_CONFIG", what .. " needs exactly one of 'src' or 'content'",
      "use src = \"path/in/files\" or content = \"literal text\"")
  end
  if e.src ~= nil then expect_type(e.src, "string", what .. ".src") end
  if e.content ~= nil then expect_type(e.content, "string", what .. ".content") end
end

local function validate_files(files)
  for i, f in ipairs(expect_list(files, "files")) do
    local what = "files[" .. i .. "]"
    expect_type(f, "table", what)
    check_fields(f, FILE_FIELDS, what)
    expect_type(f.dest, "string", what .. ".dest", "dest = \"~/.config/app/file\"")
    if not (f.dest:sub(1, 1) == "/" or f.dest == "~" or f.dest:sub(1, 2) == "~/") then
      err.raise("E_CONFIG", what .. ".dest must be absolute or start with ~/ (got " .. f.dest .. ")",
        "write dest = \"~/" .. f.dest .. "\"")
    end
    validate_src_content(f, what)
    if f.action == nil then f.action = f.src and "link" or "create" end
    expect_one_of(f.action, { "link", "copy", "create" }, what .. ".action")
    if f.action == "link" and f.content ~= nil then
      err.raise("E_CONFIG", what .. ": action 'link' needs src, not content", "use action = \"create\"")
    end
    if f.action == "create" and f.src ~= nil then
      err.raise("E_CONFIG", what .. ": action 'create' takes content, not src", "use action = \"copy\"")
    end
    if f.mode ~= nil then
      if type(f.mode) ~= "string" or not f.mode:match("^0?[0-7][0-7][0-7]$") then
        err.raise("E_CONFIG", what .. ".mode must be an octal string like \"0644\"", "")
      end
      if f.action == "link" then
        err.raise("E_CONFIG", what .. ": mode cannot be set on a link", "use action = \"copy\"")
      end
    end
    if f.scope ~= nil then expect_one_of(f.scope, { "user", "system" }, what .. ".scope") end
    if f.adopt ~= nil then expect_type(f.adopt, "boolean", what .. ".adopt") end
  end
end

local function validate_services(services)
  for i, s in ipairs(expect_list(services, "services")) do
    local what = "services[" .. i .. "]"
    expect_type(s, "table", what)
    check_fields(s, SERVICE_FIELDS, what)
    expect_type(s.unit, "string", what .. ".unit")
    s.scope = s.scope or "system"
    expect_one_of(s.scope, { "system", "user" }, what .. ".scope")
    expect_one_of(s.state, { "enabled", "disabled" }, what .. ".state")
    if s.active ~= nil then expect_type(s.active, "boolean", what .. ".active") end
  end
end

local function validate_users(users)
  for i, u in ipairs(expect_list(users, "users")) do
    local what = "users[" .. i .. "]"
    expect_type(u, "table", what)
    check_fields(u, USER_FIELDS, what)
    expect_type(u.name, "string", what .. ".name")
    if not u.name:match("^[a-z_][a-z0-9_-]*$") then
      err.raise("E_CONFIG", what .. ".name '" .. u.name .. "' is not a valid user name", "")
    end
    if u.shell ~= nil then expect_type(u.shell, "string", what .. ".shell") end
    expect_string_list(u.groups, what .. ".groups")
    if u.state ~= nil then expect_one_of(u.state, { "present" }, what .. ".state") end
  end
end

local function validate_locale(locale)
  if locale == nil then return end
  expect_type(locale, "table", "locale")
  check_fields(locale, LOCALE_FIELDS, "locale")
  expect_string_list(locale.locales, "locale.locales")
  if locale.default ~= nil then
    expect_type(locale.default, "string", "locale.default")
    local found = false
    for _, l in ipairs(locale.locales or {}) do
      if l == locale.default or l:match("^(%S+)") == locale.default then found = true end
    end
    if not found then
      err.raise("E_CONFIG", "locale.default '" .. locale.default .. "' is not in locale.locales",
        "add it to locales")
    end
  end
end

local function validate_udev(udev)
  for i, r in ipairs(expect_list(udev, "udev")) do
    local what = "udev[" .. i .. "]"
    expect_type(r, "table", what)
    check_fields(r, UDEV_FIELDS, what)
    expect_type(r.name, "string", what .. ".name")
    if not r.name:match("^[%w._-]+%.rules$") then
      err.raise("E_CONFIG", what .. ".name must look like 70-device.rules", "")
    end
    validate_src_content(r, what)
  end
end

local function validate_actions(actions)
  local names = {}
  for i, a in ipairs(expect_list(actions, "actions")) do
    if M.kind_of(a) ~= "action" then
      err.raise("E_CONFIG", "actions[" .. i .. "] must be created with rp.action { ... }", "")
    end
    if names[a.name] then
      err.raise("E_CONFIG", "duplicate action name '" .. a.name .. "'", "action names must be unique within a module")
    end
    names[a.name] = true
  end
end

function M.validate_content(decl)
  local packages = expect_string_list(decl.packages, "packages")
  -- de-duplicate preserving first occurrence
  local seen, dedup = {}, {}
  for i, p in ipairs(packages) do
    local backend, name = p:match("^([^:]*):(.*)$")
    if backend and (not backend:match("^[%w._-]+$") or name == "") then
      err.raise("E_CONFIG", "packages[" .. i .. "] '" .. p .. "' is not a package or backend:package",
        "write \"neovim\" (default backend) or \"yay:neovim\"")
    end
    if not seen[p] then
      seen[p] = true
      dedup[#dedup + 1] = p
    end
  end
  if decl.packages then decl.packages = dedup end
  validate_files(decl.files)
  validate_env(decl.env, "env")
  validate_env(decl.system_env, "system_env")
  validate_services(decl.services)
  validate_users(decl.users)
  validate_locale(decl.locale)
  validate_udev(decl.udev)
  validate_actions(decl.actions)
  return decl
end

---------------------------------------------------------------------------
-- Builders
---------------------------------------------------------------------------

local function expect_table_arg(t, fn)
  if type(t) ~= "table" then
    err.raise("E_CONFIG", fn .. " expects a table", "call it as " .. fn .. " { ... }")
  end
end

function M.module(spec)
  expect_table_arg(spec, "rp.module")
  check_fields(spec, M.MODULE_FIELDS, "module")
  if spec.name ~= nil then expect_type(spec.name, "string", "name") end
  if spec.description ~= nil then expect_type(spec.description, "string", "description") end
  if spec.provides ~= nil then
    expect_type(spec.provides, "string", "provides")
    if not M.valid_module_name(spec.provides) then
      err.raise("E_NAME", "provides '" .. spec.provides .. "' is not a valid interface name", "")
    end
  end
  if spec.priority ~= nil then
    if type(spec.priority) ~= "number" or spec.priority ~= math.floor(spec.priority) then
      err.raise("E_CONFIG", "priority must be an integer", "lower numbers are preferred; default 100")
    end
  end
  -- "name" (synced along) or { "name", optional = true } (listed, synced when asked for).
  for i, r in ipairs(expect_list(spec.requires, "requires")) do
    local name = r
    if type(r) == "table" then
      name = r[1]
      for _, k in ipairs(util.sorted_keys_of_set(r)) do
        if k ~= 1 and k ~= "optional" then
          err.raise("E_CONFIG", "unknown field '" .. tostring(k) .. "' in requires[" .. i .. "]",
            "entries look like \"dev/git\" or { \"dev/git\", optional = true }")
        end
      end
      if r.optional ~= nil then
        expect_type(r.optional, "boolean", "requires[" .. i .. "] optional", "optional = true")
      end
    end
    if type(name) ~= "string" or not M.valid_module_name(name) then
      err.raise("E_NAME", "requires[" .. i .. "] is not a valid module name",
        "module names look like dev/git (lowercase, digits, _ + -)")
    end
  end
  for i, r in ipairs(expect_string_list(spec.integrates, "integrates")) do
    if not M.valid_module_name(r) then
      err.raise("E_NAME", "integrates[" .. i .. "] '" .. r .. "' is not a valid module name",
        "module names look like dev/git (lowercase, digits, _ + -)")
    end
  end
  if spec.integrates ~= nil and #spec.integrates == 0 then
    err.raise("E_CONFIG", "integrates is empty", "list the modules this one integrates, e.g. { \"shell/fish\", \"dev/rust\" }")
  end
  if spec.auto_sync ~= nil then
    expect_type(spec.auto_sync, "boolean", "auto_sync", "auto_sync = false holds an integration until it is named")
  end
  if spec.params ~= nil then expect_type(spec.params, "table", "params") end
  if spec.with_params ~= nil then
    expect_type(spec.with_params, "function", "with_params", "with_params = function(p, sys) return { ... } end")
  end
  M.validate_content(spec)
  return tag(spec, "module")
end

function M.interface(spec)
  expect_table_arg(spec, "rp.interface")
  for _, k in ipairs(util.sorted_keys_of_set(spec)) do
    if k ~= "name" and k ~= "description" then
      local valid = set_of(M.MODULE_FIELDS)[k]
      err.raise("E_CONFIG", (valid and ("interfaces cannot declare '" .. k .. "'"))
        or ("unknown field '" .. tostring(k) .. "' in interface"),
        valid and "move content into a provider module (provides = \"<this interface>\")"
          or (util.did_you_mean(k, { "name", "description" }) or "interfaces take name and description"))
    end
  end
  if type(spec.description) ~= "string" or spec.description == "" then
    err.raise("E_CONFIG", "interface needs a description", "add description = \"what providers must do\"")
  end
  return tag(spec, "interface")
end

function M.param(spec)
  expect_table_arg(spec, "rp.param")
  check_fields(spec, PARAM_FIELDS, "rp.param")
  local params = require("repd.params")
  params.normalize(nil, nil, tag(spec, "param"))
  return spec
end

function M.action(spec)
  expect_table_arg(spec, "rp.action")
  check_fields(spec, ACTION_FIELDS, "rp.action")
  expect_type(spec.name, "string", "action name")
  expect_one_of(spec.mode, { "user", "system" }, "action '" .. spec.name .. "' mode")
  expect_type(spec.check, "function", "action '" .. spec.name .. "' check",
    "check = function(ctx) return <already done?> end")
  expect_type(spec.run, "function", "action '" .. spec.name .. "' run")
  return tag(spec, "action")
end

-- Root builders record into the active collector (only while repd.lua runs).
M.collector = nil

local function root_only(fn)
  if not M.collector then
    err.raise("E_CONFIG", fn .. "() can only be used in repd.lua", "move it to the repository's repd.lua")
  end
  return M.collector
end

function M.option(key, value)
  local c = root_only("rp.option")
  local valid = set_of(M.OPTION_KEYS)
  -- "<backend>.<name>" keys belong to add-ons; repd checks them against what
  -- the installed plugins declare.
  local prefix = type(key) == "string" and key:match("^([%w_-]+)%.[%w_-]+$")
  if not valid[key] and prefix and prefix ~= "packages" and prefix ~= "services" and prefix ~= "env" then
    expect_type(value, "string", "option " .. key)
    c.options[key] = value
    return
  end
  if key == "packages.backend" then
    err.raise("E_CONFIG", "option packages.backend was renamed to packages.default_backend",
      "rp.option(\"packages.default_backend\", " .. string.format("%q", tostring(value)) .. ")")
  end
  if not valid[key] then
    err.raise("E_CONFIG", "unknown option '" .. tostring(key) .. "'",
      util.did_you_mean(tostring(key), M.OPTION_KEYS) or ("valid options: " .. table.concat(M.OPTION_KEYS, ", ")))
  end
  if key == "env.targets" then
    -- "auto", one target, or a list of targets; stored comma-separated for the host.
    local list = type(value) == "string" and { value } or value
    if type(list) ~= "table" or #list == 0 then
      err.raise("E_CONFIG", "option env.targets must be \"auto\" or a list of targets",
        "e.g. rp.option(\"env.targets\", { \"sh\", \"fish\" })")
    end
    if not (#list == 1 and list[1] == "auto") then
      for _, t in ipairs(list) do expect_one_of(t, M.ENV_TARGETS, "env target") end
    end
    c.options[key] = table.concat(list, ",")
    return
  end
  expect_type(value, "string", "option " .. key, "use \"auto\" or a backend name")
  if key == "packages.default_backend" and value == "auto" then
    -- Packages are routed explicitly: "yay:neovim", or this default.
    err.raise("E_CONFIG", "packages.default_backend has no \"auto\": it names the default backend for unprefixed packages",
      "e.g. rp.option(\"packages.default_backend\", \"pacman\"), or prefix each package: \"yay:neovim\"")
  end
  c.options[key] = value
end

function M.pin(interface, module)
  local c = root_only("rp.pin")
  if not M.valid_module_name(interface) or not M.valid_module_name(module) then
    err.raise("E_NAME", "rp.pin expects two module names", "rp.pin(\"dev/ide\", \"dev/ide/vim\")")
  end
  c.pins[interface] = module
end

function M.profile(spec)
  local c = root_only("rp.profile")
  expect_table_arg(spec, "rp.profile")
  check_fields(spec, PROFILE_FIELDS, "profile")
  expect_type(spec.name, "string", "profile name")
  if not spec.name:match("^[a-z0-9_-]+$") then
    err.raise("E_NAME", "profile name '" .. spec.name .. "' is invalid", "use lowercase letters, digits, _ and -")
  end
  if spec.default ~= nil then expect_type(spec.default, "boolean", "profile default") end
  local use = expect_list(spec.use, "profile " .. spec.name .. " use")
  if #use == 0 then
    err.raise("E_CONFIG", "profile '" .. spec.name .. "' uses no modules", "add use = { \"base\" }")
  end
  local entries = {}
  for i, u in ipairs(use) do
    local entry
    if type(u) == "string" then
      entry = { name = u, params = {} }
    elseif type(u) == "table" then
      entry = { name = u.name or u[1], params = u.params or {}, auto_sync = u.auto_sync }
      for _, k in ipairs(util.sorted_keys_of_set(u)) do
        if k ~= 1 and k ~= "name" and k ~= "params" and k ~= "auto_sync" then
          err.raise("E_CONFIG", "unknown field '" .. tostring(k) .. "' in profile " .. spec.name .. " use[" .. i .. "]",
            "entries look like { \"module\", params = { ... } } or { \"module\", auto_sync = false }")
        end
      end
      if u.auto_sync ~= nil then
        expect_type(u.auto_sync, "boolean", "profile " .. spec.name .. " use[" .. i .. "] auto_sync",
          "true or false")
      end
    end
    if not entry or not M.valid_module_name(entry.name) then
      err.raise("E_NAME", "profile " .. spec.name .. " use[" .. i .. "] is not a module name", "")
    end
    entries[#entries + 1] = entry
  end
  local providers = spec.providers or {}
  expect_type(providers, "table", "profile providers")
  for _, p in ipairs(c.profiles) do
    if p.name == spec.name then
      err.raise("E_CONFIG", "profile '" .. spec.name .. "' is declared twice", "profile names must be unique")
    end
  end
  c.profiles[#c.profiles + 1] = {
    name = spec.name, description = spec.description or "", default = spec.default == true,
    use = entries, providers = providers,
  }
end

function M.backend(spec)
  expect_table_arg(spec, "rp.backend")
  check_fields(spec, BACKEND_FIELDS, "rp.backend")
  expect_one_of(spec.namespace, { "packages", "services" }, "backend namespace")
  expect_type(spec.name, "string", "backend name")
  if not spec.name:match("^[a-z0-9_-]+$") then
    err.raise("E_NAME", "backend name '" .. spec.name .. "' is invalid", "use lowercase letters, digits, _ and -")
  end
  spec.privilege = spec.privilege or (spec.namespace == "packages" and "system" or "user")
  expect_one_of(spec.privilege, { "user", "system" }, "backend privilege")
  expect_type(spec.ops, "table", "backend ops")
  local required = spec.namespace == "packages" and { "probe", "installed", "install" }
    or { "probe", "state", "enable", "disable" }
  for _, op in ipairs(required) do
    expect_type(spec.ops[op], "function", "backend " .. spec.name .. " op '" .. op .. "'")
  end
  return tag(spec, "backend")
end

return M
