"""Params passed straight from with_params into a declaration (read-only proxies)."""

from conftest import RepdTestCase, main

LOCALE = '''return rp.module {
  params = { locales = { "en_US.UTF-8 UTF-8", "de_DE.UTF-8 UTF-8" }, vars = { EDITOR = "nvim" } },
  with_params = function(p)
    return {
      locale = { locales = p.locales, default = "en_US.UTF-8" },
      system_env = p.vars,
      env = { LOCALES = tostring(#p.locales) },
    }
  end,
}
'''


class ParamsProxies(RepdTestCase):
    def test_list_param_into_locale_locales(self):
        self.sb.write("repd.lua", 'rp.profile { name = "main", default = true, use = { "system" } }\n')
        self.sb.write("modules/system.lua", LOCALE)
        r = self.sb.run("show", "system")
        self.assertOk(r)
        self.assertIn('"en_US.UTF-8 UTF-8"', r.out)
        self.assertIn('"de_DE.UTF-8 UTF-8"', r.out)
        self.assertIn('EDITOR = "nvim"', r.out)
        self.assertIn('LOCALES = "2"', r.out)

    def test_action_built_from_params_in_with_params(self):
        # rp.action values are recognised by table identity: unwrapping params
        # must not copy them (system/time builds its time zone action this way).
        self.sb.write("repd.lua", 'rp.profile { name = "main", default = true, use = { "system/time" } }\n')
        self.sb.write("modules/system/time.lua", """return rp.module {
  params = { zone = "UTC", extra = { "a", "b" } },
  with_params = function(p)
    return { actions = { rp.action { name = "tz " .. p.zone, mode = "user",
      check = function(ctx) return #ctx.params.extra == 2 and false end,
      run = function(ctx) end } } }
  end,
}
""")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertIn("action  tz UTC", r.out)


if __name__ == "__main__":
    main()
