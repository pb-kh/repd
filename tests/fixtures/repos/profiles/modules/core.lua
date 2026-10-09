return rp.module {
  description = "Core settings",
  params = { motd = rp.param { default = "hello", description = "Login greeting" } },
  with_params = function(p) return { files = { { dest = "~/.motd", content = p.motd .. "\n" } } } end,
}
