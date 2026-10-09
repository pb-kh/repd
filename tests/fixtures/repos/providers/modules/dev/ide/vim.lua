return rp.module {
  description = "Vim setup",
  provides = "dev/ide",
  priority = 10,
  env = { VISUAL = "vim" },
}
