-- Byte-stable canonical encoding (research R8, SC-003).
local lu = require("luaunit")
local canon = require("repd.canon")

TestCanon = {}

function TestCanon.test_scalars()
  lu.assertEquals(canon.encode(nil), "nil")
  lu.assertEquals(canon.encode(true), "true")
  lu.assertEquals(canon.encode(false), "false")
  lu.assertEquals(canon.encode("a\"b\n"), '"a\\"b\\n"')
end

function TestCanon.test_integers_and_floats_are_normalised()
  lu.assertEquals(canon.encode(3), "3")
  lu.assertEquals(canon.encode(3.0), "3")
  lu.assertEquals(canon.encode(-0.0), "0")
  lu.assertEquals(canon.encode(0.5), "0.5")
  lu.assertEquals(canon.encode(2 ^ 40), "1099511627776")
  lu.assertEquals(canon.encode(0.1), "0.10000000000000001")
end

function TestCanon.test_rejects_non_finite_numbers()
  lu.assertErrorMsgContains("not finite", canon.encode, 1 / 0)
  lu.assertErrorMsgContains("not finite", canon.encode, 0 / 0)
end

function TestCanon.test_lists_and_maps()
  lu.assertEquals(canon.encode({}), "{}")
  lu.assertEquals(canon.encode({ "a", "b", 3 }), '{"a","b",3}')
  lu.assertEquals(canon.encode({ b = 1, a = 2 }), "{a=2,b=1}")
  lu.assertEquals(canon.encode({ ["dev/ide"] = 1, x = 2 }), '{["dev/ide"]=1,x=2}')
  -- keyword keys must be quoted
  lu.assertEquals(canon.encode({ ["end"] = 1 }), '{["end"]=1}')
end

function TestCanon.test_mixed_keys_sorted_by_type_then_value()
  lu.assertEquals(canon.encode({ [2] = "x", [10] = "y", z = true, [true] = 1 }),
    '{[true]=1,[2]="x",[10]="y",z=true}')
end

function TestCanon.test_sparse_array_is_a_map()
  lu.assertEquals(canon.encode({ [1] = "a", [3] = "c" }), '{[1]="a",[3]="c"}')
end

function TestCanon.test_insertion_order_does_not_matter()
  local a, b = {}, {}
  local keys = { "zeta", "alpha", "mid", "beta", "omega", "k1", "k2", "k10" }
  for i = 1, #keys do a[keys[i]] = i end
  for i = #keys, 1, -1 do b[keys[i]] = i end
  lu.assertEquals(canon.encode(a), canon.encode(b))
  lu.assertEquals(canon.pretty(a), canon.pretty(b))
end

function TestCanon.test_rejects_functions_and_cycles()
  lu.assertErrorMsgContains("function", canon.encode, { f = function() end })
  local t = {}
  t.self = t
  lu.assertErrorMsgContains("cycle", canon.encode, t)
end

function TestCanon.test_shared_subtable_is_not_a_cycle()
  local shared = { 1 }
  lu.assertEquals(canon.encode({ a = shared, b = shared }), "{a={1},b={1}}")
end

function TestCanon.test_function_hook()
  local s = canon.encode({ f = print }, { on_function = function() return "<fn>" end })
  lu.assertEquals(s, '{f="<fn>"}')
end

function TestCanon.test_pretty()
  local text = canon.pretty({ version = 1, providers = { ["dev/ide"] = { provider = "dev/ide/vim" } }, l = { "a", "b" }, e = {} })
  lu.assertEquals(text, table.concat({
    "{",
    "  e = {},",
    "  l = {",
    '    "a",',
    '    "b",',
    "  },",
    "  providers = {",
    '    ["dev/ide"] = {',
    '      provider = "dev/ide/vim",',
    "    },",
    "  },",
    "  version = 1,",
    "}",
  }, "\n"))
end

function TestCanon.test_round_trip()
  local v = { a = { 1, 2, { x = "y" } }, ["weird key"] = "q\0z", n = -12.25 }
  local back = assert(loadstring("return " .. canon.encode(v)))()
  lu.assertEquals(back, v)
  local back2 = assert(loadstring("return " .. canon.pretty(v)))()
  lu.assertEquals(back2, v)
end

function TestCanon.test_sorted_pairs()
  local seen = {}
  for k in canon.sorted_pairs({ c = 1, a = 2, b = 3 }) do seen[#seen + 1] = k end
  lu.assertEquals(seen, { "a", "b", "c" })
end

os.exit(lu.LuaUnit.run())
