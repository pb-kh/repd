return rp.module {
  description = "Emacs setup",
  provides = "dev/ide",
  priority = 20,
  env = { VISUAL = "emacs" },
}
