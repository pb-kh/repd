return rp.module {
  description = "Terminal emulator",
  params = { font_size = rp.param { default = 11, type = "number" }, theme = rp.param { default = "dark", one_of = { "dark", "light" } } },
  with_params = function(p)
    return { files = { { dest = "~/.config/term.conf", content = "font_size=" .. p.font_size .. "\ntheme=" .. p.theme .. "\n" } } }
  end,
}
