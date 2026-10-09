-- Structured errors raised by the bootstrap (contracts/cli.md error contract).
-- Raised as tables so the C++ host can map them to `error[E_CODE]` output.
local M = {}

local ERR_MT = {}
ERR_MT.__index = ERR_MT
ERR_MT.__tostring = function(e)
  return "error[" .. e.code .. "]: " .. e.message
end

function M.new(code, message, hint)
  return setmetatable({ code = code, message = message, hint = hint or "" }, ERR_MT)
end

function M.raise(code, message, hint)
  error(M.new(code, message, hint), 0)
end

function M.is(e)
  return getmetatable(e) == ERR_MT
end

-- "file:line" of the Lua function `level` frames above the caller.
function M.where(level)
  local info = debug.getinfo((level or 1) + 1, "Sl")
  if not info or info.currentline < 0 then return nil end
  return info.short_src .. ":" .. info.currentline
end

-- Normalises any error value into a structured error, attaching context.
function M.wrap(e, code, context)
  if M.is(e) then
    if context and not e.context then
      e.context = context
      e.message = e.message .. " (" .. context .. ")"
    end
    return e
  end
  local msg = tostring(e)
  if context then msg = msg .. " (" .. context .. ")" end
  return M.new(code or "E_CONFIG", msg, "fix the Lua error at the reported location")
end

return M
