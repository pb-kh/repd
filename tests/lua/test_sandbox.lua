-- Evaluation sandbox restrictions (research R9, FR-018).
local lu = require("luaunit")
local sandbox = require("repro.sandbox")
local repro = require("repro")
local err = require("repro.err")

local function run(src)
  local env = sandbox.new_env(repro.rp)
  local fn = assert(loadstring(src, "@modules/test.lua"))
  setfenv(fn, env)
  return pcall(fn)
end

local function expect_error(src, needle)
  local ok, e = run(src)
  lu.assertFalse(ok, "expected failure for: " .. src)
  local msg = err.is(e) and (e.code .. ": " .. e.message .. " / " .. (e.hint or "")) or tostring(e)
  lu.assertStrContains(msg, needle)
  return e
end

TestSandbox = {}

function TestSandbox.test_forbidden_globals()
  for _, name in ipairs({ "io", "os", "require", "debug", "ffi", "jit", "package", "load",
    "loadstring", "loadfile", "dofile", "collectgarbage" }) do
    expect_error("return " .. name, "'" .. name .. "'")
  end
end

function TestSandbox.test_math_random_removed()
  local ok, v = run("return math.random")
  lu.assertTrue(ok)
  lu.assertNil(v)
  ok, v = run("return math.floor(2.5)")
  lu.assertTrue(ok)
  lu.assertEquals(v, 2)
end

function TestSandbox.test_allowed_globals()
  local ok, v = run([[
    local t = {}
    for i, x in ipairs({3, 1, 2}) do t[#t + 1] = x end
    table.sort(t)
    return select("#", unpack(t)) .. tostring(type(pairs)) .. string.format("%d", tonumber("7"))
  ]])
  lu.assertTrue(ok, tostring(v))
  lu.assertEquals(v, "3function7")
end

function TestSandbox.test_undefined_global_suggests_rp()
  local e = expect_error("return packages", "E_CONFIG")
  lu.assertStrContains(e.hint, "rp.packages")
  e = expect_error("return module { }", "E_CONFIG")
  lu.assertStrContains(e.hint, "rp.module")
end

function TestSandbox.test_undefined_global_reports_location()
  local e = expect_error("local x = 1\nreturn nosuch", "nosuch")
  lu.assertStrContains(e.message, "modules/test.lua:2")
end

function TestSandbox.test_rp_is_read_only()
  expect_error("rp.module = nil", "read-only")
end

function TestSandbox.test_libraries_are_per_env_copies()
  local ok = run("string.upper = nil")
  lu.assertTrue(ok)
  lu.assertNotNil(string.upper)
end

function TestSandbox.test_globals_can_be_defined()
  local ok, v = run("helper = function() return 5 end return helper()")
  lu.assertTrue(ok)
  lu.assertEquals(v, 5)
end

function TestSandbox.test_read_only_queries_allowed()
  local ok, v = run("return rp.fs.exists('~/definitely-not-here') == false and type(rp.sys.hostname()) == 'string'")
  lu.assertTrue(ok, tostring(v))
  lu.assertTrue(v)
end

function TestSandbox.test_mutating_facade_rejected_in_eval()
  expect_error("rp.packages.install({'clang'})", "E_DECLARATIVE")
  expect_error("rp.services.enable('sshd')", "E_DECLARATIVE")
end

-- Params reach with_params and actions as read-only proxies (LuaJIT has no
-- __len/__pairs/__ipairs), yet they must read like plain tables.
local function params_env(values)
  local env = sandbox.new_env(repro.rp)
  env.p = sandbox.read_only(values, "params")
  return env
end

local function run_with(env, src)
  local fn = assert(loadstring(src, "@modules/test.lua"))
  setfenv(fn, env)
  return pcall(fn)
end

function TestSandbox.test_read_only_lists_read_like_tables()
  local env = params_env({ locales = { "en_US.UTF-8 UTF-8", "de_DE.UTF-8 UTF-8" } })
  local ok, v = run_with(env, [[
    local seen = {}
    for i, l in ipairs(p.locales) do seen[#seen + 1] = i .. "=" .. l end
    return { n = #p.locales, seen = table.concat(seen, ";"), joined = table.concat(p.locales, ","),
             first = (unpack(p.locales)) }
  ]])
  lu.assertTrue(ok, tostring(v))
  lu.assertEquals(v, { n = 2, seen = "1=en_US.UTF-8 UTF-8;2=de_DE.UTF-8 UTF-8",
    joined = "en_US.UTF-8 UTF-8,de_DE.UTF-8 UTF-8", first = "en_US.UTF-8 UTF-8" })
end

function TestSandbox.test_read_only_maps_iterate_with_pairs_and_next()
  local env = params_env({ vars = { A = "1", B = { "x" } } })
  local ok, v = run_with(env, [[
    local keys = {}
    for k, val in pairs(p.vars) do keys[#keys + 1] = k .. ":" .. type(val) end
    table.sort(keys)
    return { keys = table.concat(keys, ","), has = next(p.vars) ~= nil, inner = #p.vars.B }
  ]])
  lu.assertTrue(ok, tostring(v))
  lu.assertEquals(v, { keys = "A:string,B:table", has = true, inner = 1 })
end

function TestSandbox.test_read_only_still_refuses_writes()
  local env = params_env({ theme = "dark", list = { "a" } })
  for _, src in ipairs({ "p.theme = 'x'", "p.list[2] = 'b'", "table.insert(p.list, 'b')",
    "table.remove(p.list)", "table.sort(p.list)" }) do
    local ok, e = run_with(env, src)
    lu.assertFalse(ok, src)
    lu.assertStrContains(err.is(e) and e.message or tostring(e), "read-only")
  end
end

function TestSandbox.test_plain_unwraps_nested_proxies()
  local ro = sandbox.read_only({ locales = { "a", "b" }, env = { K = "v" } }, "params")
  local frag = sandbox.plain({ locale = { locales = ro.locales }, env = ro.env, list = { ro.env } })
  lu.assertEquals(frag, { locale = { locales = { "a", "b" } }, env = { K = "v" }, list = { { K = "v" } } })
  lu.assertNil(getmetatable(frag.locale.locales))
  lu.assertNil(getmetatable(frag.env))
end

function TestSandbox.test_plain_keeps_builder_values()
  local builders = require("repro.builders")
  local ro = sandbox.read_only({ zone = "UTC", files = { "a" } }, "params")
  local action = repro.rp.action { name = "tz " .. ro.zone, mode = "user",
    check = function() return true end, run = function() end }
  action.extra = ro.files  -- a proxy inside a tagged table
  local frag = sandbox.plain({ actions = { action } })
  lu.assertIs(frag.actions[1], action)
  lu.assertEquals(builders.kind_of(frag.actions[1]), "action")
  lu.assertFalse(sandbox.is_read_only(frag.actions[1].extra))
  lu.assertEquals(frag.actions[1].extra, { "a" })
end

os.exit(lu.LuaUnit.run())
