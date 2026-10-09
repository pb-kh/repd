return rp.module {
  description = "System accounts",
  users = { { name = "builder", shell = "/bin/zsh", groups = { "wheel", "audio" } } },
  files = { { dest = "/etc/motd", content = "Welcome\n" } },
}
