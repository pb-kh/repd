-- Fake packages backend for sandboxed tests (research R18): "installs" by
-- recording names in the backend's scratch store. Names starting with
-- "fail" cannot be installed.
return rp.backend {
  namespace = "packages",
  name = "fake",
  privilege = "user",
  ops = {
    probe = function(ctx) return true end,
    installed = function(ctx, names)
      local db = ctx.mem.fetch("installed", {})
      local out = {}
      for _, n in ipairs(names) do out[n] = db[n] == true end
      return out
    end,
    install = function(ctx, names)
      for _, n in ipairs(names) do
        if n:sub(1, 4) == "fail" then error("cannot install " .. n) end
      end
      ctx.mem.update("installed", names, true)
      ctx.log("installed " .. table.concat(names, " "))
    end,
  },
}
