-- Every public rp.* symbol must have a heading in docs/lua-api.md (research R18).
-- Usage: repd-lua-test test_api_docs.lua <path to docs/lua-api.md>
local lu = require("luaunit")
local repd = require("repd")

local DOCS = assert(arg[1], "pass the path to docs/lua-api.md")

TestApiDocs = {}

function TestApiDocs.test_every_symbol_documented()
  local f = assert(io.open(DOCS, "r"))
  local text = f:read("*a")
  f:close()
  local headings = {}
  for line in text:gmatch("[^\n]+") do
    if line:match("^#+ ") then headings[#headings + 1] = line end
  end
  local missing = {}
  for _, sym in ipairs(repd.api_registry()) do
    local found = false
    for _, h in ipairs(headings) do
      if h:find("`" .. sym, 1, true) then found = true end
    end
    if not found then missing[#missing + 1] = sym end
  end
  lu.assertEquals(missing, {}, "undocumented rp symbols")
end

function TestApiDocs.test_registry_is_sorted_and_nonempty()
  local reg = repd.api_registry()
  lu.assertTrue(#reg > 20)
  for i = 2, #reg do lu.assertTrue(reg[i - 1] < reg[i]) end
end

os.exit(lu.LuaUnit.run("--verbose"))
