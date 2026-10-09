-- US5 fixture: core works with zero backends installed.
return rp.module {
  files = {
    { dest = "~/.gitconfig", src = "gitconfig" },
    { dest = "~/.inputrc", content = "set editing-mode vi\n" },
  },
  env = { LESS = "-R" },
}
