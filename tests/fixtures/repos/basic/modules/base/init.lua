-- US1 fixture: one file, one environment variable, one package (fake backend).
return rp.module {
  description = "Base environment",
  files = {
    { dest = "~/.config/app/config.toml", src = "config.toml", action = "copy" },
  },
  env = { EDITOR = "vi" },
  packages = { "ripgrep" },
}
