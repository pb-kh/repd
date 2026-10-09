return rp.module {
  description = "User shell config",
  files = { { dest = "~/.zshrc", content = "# zsh\n" } },
  env = { SHELL_THEME = "plain" },
  system_env = { REPRO_SITE = "lab" },
}
