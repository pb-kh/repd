-- Parameter declaration, type inference, validation and resolution (FR-017).
local lu = require("luaunit")
local params = require("repd.params")
local builders = require("repd.builders")
local err = require("repd.err")

local function expect(code, fn, ...)
  local ok, e = pcall(fn, ...)
  lu.assertFalse(ok)
  lu.assertTrue(err.is(e), tostring(e))
  lu.assertEquals(e.code, code)
  return e
end

TestParams = {}

function TestParams.test_type_inferred_from_default()
  lu.assertEquals(params.normalize("m", "a", builders.param { default = "x" }).type, "string")
  lu.assertEquals(params.normalize("m", "a", builders.param { default = 3 }).type, "number")
  lu.assertEquals(params.normalize("m", "a", builders.param { default = false }).type, "boolean")
  lu.assertEquals(params.normalize("m", "a", builders.param { default = { "x" } }).type, "list")
  lu.assertEquals(params.normalize("m", "a", builders.param { default = { k = 1 } }).type, "map")
end

function TestParams.test_plain_value_shorthand()
  local p = params.normalize("m", "theme", "dark")
  lu.assertEquals(p.type, "string")
  lu.assertEquals(p.default, "dark")
end

function TestParams.test_default_must_match_declared_type()
  expect("E_PARAM", builders.param, { type = "number", default = "x" })
  expect("E_PARAM", builders.param, { default = "x", one_of = { "a", "b" } })
  expect("E_CONFIG", builders.param, { defualt = "x" })
end

function TestParams.test_resolve_defaults_and_overrides()
  local decls = {
    theme = params.normalize("m", "theme", builders.param { default = "dark", one_of = { "dark", "light" } }),
    size = params.normalize("m", "size", builders.param { default = 12 }),
  }
  lu.assertEquals(params.resolve("m", decls, nil), { theme = "dark", size = 12 })
  lu.assertEquals(params.resolve("m", decls, { theme = "light" }), { theme = "light", size = 12 })
end

function TestParams.test_override_validation()
  local decls = {
    theme = params.normalize("m", "theme", builders.param { default = "dark", one_of = { "dark", "light" } }),
  }
  local e = expect("E_PARAM", params.resolve, "m", decls, { theme = "blue" })
  lu.assertStrContains(e.message, "theme")
  lu.assertStrContains(e.message, "blue")
  lu.assertStrContains(e.hint, "dark")
  e = expect("E_PARAM", params.resolve, "m", decls, { them = "dark" })
  lu.assertStrContains(e.hint, "theme")
  expect("E_PARAM", params.resolve, "m", decls, { theme = 3 })
end

function TestParams.test_required_param_without_default()
  local decls = { name = params.normalize("m", "name", builders.param { type = "string" }) }
  local e = expect("E_PARAM", params.resolve, "m", decls, {})
  lu.assertStrContains(e.message, "name")
  lu.assertEquals(params.resolve("m", decls, { name = "x" }), { name = "x" })
end

function TestParams.test_list_and_map_types()
  local decls = {
    l = params.normalize("m", "l", builders.param { type = "list", default = {} }),
    m = params.normalize("m", "m", builders.param { type = "map", default = {} }),
  }
  lu.assertEquals(params.resolve("m", decls, { l = { "a" }, m = { k = "v" } }).l, { "a" })
  expect("E_PARAM", params.resolve, "m", decls, { l = { k = "v" } })
end

function TestParams.test_with_params_receives_read_only_params()
  local repd = require("repd")
  local merge = require("repd.merge")
  local sandbox = require("repd.sandbox")
  local decls = { theme = params.normalize("m", "theme", builders.param { default = "dark" }) }
  local values = params.resolve("m", decls, { theme = "light" })
  local fn = function(p)
    lu.assertErrorMsgContains("read-only", function() p.theme = "x" end)
    return { env = { THEME = p.theme } }
  end
  local frag = fn(sandbox.read_only(values, "params"), repd.rp.sys)
  lu.assertEquals(merge.merge("m", { env = { A = "1" } }, frag).env, { A = "1", THEME = "light" })
end

os.exit(lu.LuaUnit.run())
