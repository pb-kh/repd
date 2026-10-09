-- Determinism lint (research R9): the bootstrap must not iterate tables with
-- raw pairs() where order could leak into output. Each remaining pairs() call
-- must carry an "-- order-insensitive" annotation on the same line.
-- Usage: repro-lua-test test_lint.lua <src/lua/repro dir>
local lu = require("luaunit")

local DIR = assert(arg[1], "pass the bootstrap directory")
local FILES = { "init", "err", "util", "canon", "sandbox", "builders", "params", "merge", "lock", "backend" }

TestLint = {}

function TestLint.test_no_unsorted_pairs()
  local offenders = {}
  for _, name in ipairs(FILES) do
    local path = DIR .. "/" .. name .. ".lua"
    local n = 0
    for line in io.lines(path) do
      n = n + 1
      local code = line:gsub("%-%-.*$", "")
      local raw = code:gsub("ipairs%(", ""):gsub("sorted_pairs%(", "")
      if raw:find("pairs%(") and not line:find("order%-insensitive") then
        offenders[#offenders + 1] = name .. ".lua:" .. n .. ": " .. line
      end
    end
  end
  lu.assertEquals(offenders, {}, "unannotated pairs() in the bootstrap")
end

os.exit(lu.LuaUnit.run("--verbose"))
