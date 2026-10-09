"""US4 acceptance 3-5: a profile selects its modules, params and provider overrides."""

from conftest import ReproTestCase, main


class Profiles(ReproTestCase):
    fixture = "profiles"

    def test_laptop_profile(self):
        r = self.sb.run("sync", "-u", "--profile", "laptop", "--dry-run")
        self.assertOk(r)
        self.assertIn("profile: laptop", r.out)
        self.assertIn("dev/ide = dev/ide/vim (pin)", r.out)
        self.assertIn("modules (4):", r.out)
        self.assertIn("  terminal ", r.out)
        self.assertNotIn("  ssh ", r.out)

    def test_server_profile_subset_and_provider_override(self):
        r = self.sb.run("sync", "-u", "--profile", "server", "--dry-run")
        self.assertOk(r)
        self.assertIn("profile: server", r.out)
        self.assertIn("dev/ide = dev/ide/nano (profile)", r.out)  # level 2 beats the pin (level 3)
        self.assertIn("  ssh ", r.out)
        self.assertNotIn("  terminal ", r.out)
        self.assertIn("EDITOR=nano", r.out)

    def test_profile_params_reach_the_plan(self):
        self.assertOk(self.sb.run("sync", "-u", "--profile", "laptop"))
        self.assertEqual(self.sb.home_path(".config/term.conf").read_text(), "font_size=14\ntheme=dark\n")
        self.assertEqual(self.sb.home_path(".motd").read_text(), "hello\n")
        st = self.sb.run("status", "--profile", "laptop")
        self.assertRegex(st.out, r"terminal\s+applied")

    def test_changing_a_profile_param_makes_the_module_stale(self):
        self.assertOk(self.sb.run("sync", "-u"))
        text = (self.sb.config / "repro.lua").read_text().replace("font_size = 14", "font_size = 16")
        self.sb.write("repro.lua", text)
        st = self.sb.run("status")
        self.assertRegex(st.out, r"terminal\s+stale")
        self.assertRegex(st.out, r"core\s+applied")
        r = self.sb.run("sync", "-u")
        self.assertIn("~ files   ~/.config/term.conf  update (content differs)", r.out)

    def test_invalid_param_names_module_param_value_and_allowed(self):
        text = (self.sb.config / "repro.lua").read_text().replace(
            "params = { font_size = 14 }", 'params = { font_size = 14, theme = "blue" }')
        self.sb.write("repro.lua", text)
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_PARAM")
        for needle in ("terminal", "theme", '"blue"', '"dark", "light"'):
            self.assertIn(needle, r.err)

    def test_list_profiles(self):
        r = self.sb.run("list", "profiles")
        self.assertOk(r)
        self.assertRegex(r.out, r"laptop\s+\(default\)\s+Laptop with GUI\s+use: core, terminal, dev/tools")
        self.assertRegex(r.out, r"server\s+Headless server\s+use: core, dev/tools, ssh")

    def test_unknown_profile(self):
        r = self.sb.run("sync", "-u", "--profile", "sever", "--dry-run")
        self.assertError(r, "E_UNKNOWN")
        self.assertIn("did you mean 'server'?", r.err)

    def test_explicit_modules_with_profile_overrides(self):
        r = self.sb.run("sync", "-u", "terminal", "--dry-run")
        self.assertOk(r)
        self.assertIn("modules (1):", r.out)
        self.assertIn("font_size=14", (self.sb.run("show", "terminal").out))


if __name__ == "__main__":
    main()
