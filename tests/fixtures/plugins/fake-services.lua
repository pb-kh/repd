-- Fake services backend for sandboxed tests: unit state lives in the
-- backend's scratch store.
local function get(ctx, unit)
  local enabled = ctx.mem.fetch("enabled", {})
  local active = ctx.mem.fetch("active", {})
  return { enabled = enabled[unit] == true, active = active[unit] == true }
end

return rp.backend {
  namespace = "services",
  name = "fake",
  privilege = "user",
  ops = {
    probe = function(ctx) return true end,
    state = function(ctx, unit, scope) return get(ctx, unit) end,
    enable = function(ctx, unit) ctx.mem.update("enabled", { unit }, true) end,
    disable = function(ctx, unit) ctx.mem.update("enabled", { unit }, false) end,
    start = function(ctx, unit) ctx.mem.update("active", { unit }, true) end,
    stop = function(ctx, unit) ctx.mem.update("active", { unit }, false) end,
  },
}
