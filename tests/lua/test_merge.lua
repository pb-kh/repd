-- with_params fragment merging (research R11).
local lu = require("luaunit")
local merge = require("repro.merge")
local err = require("repro.err")

local function expect(code, fn, ...)
  local ok, e = pcall(fn, ...)
  lu.assertFalse(ok)
  lu.assertTrue(err.is(e), tostring(e))
  lu.assertEquals(e.code, code)
  return e
end

TestMerge = {}

function TestMerge.test_lists_concatenate_and_dedup()
  local base = { packages = { "git", "vim" }, files = { { dest = "~/a", content = "x" } } }
  local out = merge.merge("m", base, { packages = { "vim", "tmux", "git", "zsh" }, files = { { dest = "~/a", content = "x" }, { dest = "~/b", content = "y" } } })
  lu.assertEquals(out.packages, { "git", "vim", "tmux", "zsh" })
  lu.assertEquals(#out.files, 2)
  lu.assertEquals(out.files[2].dest, "~/b")
end

function TestMerge.test_base_is_not_mutated()
  local base = { packages = { "git" } }
  merge.merge("m", base, { packages = { "vim" } })
  lu.assertEquals(base.packages, { "git" })
end

function TestMerge.test_maps_merge()
  local out = merge.merge("m", { env = { EDITOR = "vim" } }, { env = { PAGER = "less", EDITOR = "vim" } })
  lu.assertEquals(out.env, { EDITOR = "vim", PAGER = "less" })
end

function TestMerge.test_map_conflict()
  local e = expect("E_CONFLICT", merge.merge, "dev/x", { env = { EDITOR = "vim" } }, { env = { EDITOR = "emacs" } })
  lu.assertStrContains(e.message, "EDITOR")
  lu.assertStrContains(e.message, "dev/x")
end

function TestMerge.test_locale_merge()
  local out = merge.merge("m", { locale = { locales = { "en_US.UTF-8 UTF-8" } } },
    { locale = { locales = { "de_DE.UTF-8 UTF-8" }, default = "en_US.UTF-8" } })
  lu.assertEquals(out.locale.locales, { "en_US.UTF-8 UTF-8", "de_DE.UTF-8 UTF-8" })
  lu.assertEquals(out.locale.default, "en_US.UTF-8")
  expect("E_CONFLICT", merge.merge, "m", { locale = { locales = {}, default = "a" } }, { locale = { locales = {}, default = "b" } })
end

function TestMerge.test_non_content_fields_rejected()
  local e = expect("E_CONFIG", merge.merge, "m", {}, { requires = { "x" } })
  lu.assertStrContains(e.message, "requires")
  e = expect("E_CONFIG", merge.merge, "m", {}, { pakages = { "x" } })
  lu.assertStrContains(e.hint, "packages")
end

function TestMerge.test_nil_fragment_is_noop()
  lu.assertEquals(merge.merge("m", { packages = { "a" } }, nil).packages, { "a" })
end

os.exit(lu.LuaUnit.run())
