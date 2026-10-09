"""US1: declare once, preview, apply, re-apply as a fast no-op (acceptance 1-3)."""

import time

from conftest import ReproTestCase, main


class DeclareApply(ReproTestCase):
    fixture = "basic"

    def test_init_scaffolds_and_applies(self):
        self.sb.cleanup()
        from conftest import Sandbox
        self.sb = Sandbox(None)
        r = self.sb.run("init")
        self.assertOk(r)
        for rel in ("repro.lua", "modules/base/init.lua", "modules/base/files/example.conf", ".gitignore", "plugins/lua"):
            self.assertTrue((self.sb.config / rel).exists(), rel)
        self.assertError(self.sb.run("init"), "E_CONFIG")  # refuses to overwrite
        self.assertOk(self.sb.run("init", "--force"))
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertTrue(self.sb.home_path(".config/example.conf").exists())

    def test_preview_changes_nothing(self):
        before = self.sb.snapshot(self.sb.tmp)
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertIn("repro plan — mode: user — profile: main", r.out)
        self.assertIn("+ files   ~/.config/app/config.toml  copy from modules/base/files/config.toml", r.out)
        self.assertIn("+ env     EDITOR=vi  (~/.local/share/repro/env.d/base.sh)", r.out)
        self.assertIn("+ packages fake: install ripgrep", r.out)
        self.assertIn("packages = fake lua:", r.out)
        self.assertIn("reduced trust", r.out)
        self.assertIn("summary: 4 changes in this pass", r.out)
        self.assertEqual(self.sb.snapshot(self.sb.tmp), before, "dry-run must not write anything")

    def test_apply_then_noop(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        for line in ("ok    base/packages/fake",
                     "ok    base/files/~/.config/app/config.toml",
                     "ok    base/env/~/.local/share/repro/env.d/base.sh",
                     "ok    base/env/~/.profile",
                     "done: 4 applied"):
            self.assertIn(line, r.out)
        self.assertEqual(self.sb.home_path(".config/app/config.toml").read_text(), 'theme = "dark"\n')
        env = self.sb.home_path(".local/share/repro/env.d/base.sh").read_text()
        self.assertIn("export EDITOR='vi'\n", env)
        self.assertIn("# BEGIN repro\n", self.sb.home_path(".profile").read_text())

        start = time.monotonic()
        r = self.sb.run("sync", "-u")
        elapsed = time.monotonic() - start
        self.assertOk(r)
        self.assertIn("base                 up to date", r.out)
        self.assertIn("summary: up to date", r.out)
        self.assertTrue(r.out.rstrip().endswith("up to date"))
        self.assertLess(elapsed, 2.0)

    def test_status_and_journal(self):
        r = self.sb.run("status")
        self.assertOk(r)
        self.assertRegex(r.out, r"base\s+pending")
        self.assertOk(self.sb.run("sync", "-u"))
        r = self.sb.run("status")
        self.assertRegex(r.out, r"base\s+applied\s+#1")
        r = self.sb.run("journal")
        self.assertOk(r)
        self.assertIn("run #1  user  profile main", r.out)
        self.assertIn("ok    base/packages/fake", r.out)
        # Changing the declaration makes the module stale.
        self.sb.write("modules/base/init.lua", (self.sb.config / "modules/base/init.lua").read_text()
                      .replace('EDITOR = "vi"', 'EDITOR = "nvim"'))
        r = self.sb.run("status")
        self.assertRegex(r.out, r"base\s+stale")

    def test_show_and_list(self):
        r = self.sb.run("show", "base")
        self.assertOk(r)
        self.assertIn("module base", r.out)
        self.assertIn('EDITOR = "vi"', r.out)
        self.assertIn("base/files/~/.config/app/config.toml", r.out)
        r = self.sb.run("list", "modules")
        self.assertOk(r)
        self.assertIn("base", r.out)
        r = self.sb.run("list", "profiles")
        self.assertIn("main  (default)", r.out)


if __name__ == "__main__":
    main()
