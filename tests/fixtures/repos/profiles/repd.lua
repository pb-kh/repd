-- US4 fixture: two profiles sharing core modules with different parameters/providers.
rp.pin("dev/ide", "dev/ide/vim")
rp.profile {
  name = "laptop",
  description = "Laptop with GUI",
  default = true,
  use = { "core", { "terminal", params = { font_size = 14 } }, "dev/tools" },
}
rp.profile {
  name = "server",
  description = "Headless server",
  use = { "core", "dev/tools", "ssh" },
  providers = { ["dev/ide"] = "dev/ide/nano" },
}
