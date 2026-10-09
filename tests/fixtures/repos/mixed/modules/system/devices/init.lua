return rp.module {
  description = "udev rules",
  udev = { { name = "70-keyboard.rules", src = "70-keyboard.rules" } },
}
