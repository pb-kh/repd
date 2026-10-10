"""Packages are installed in one step per backend for as many modules as the
dependency order allows; the ledger and `status` still attribute each package
to the module that declares it."""

import sqlite3

from conftest import ReproTestCase, main


class PackageBatches(ReproTestCase):
    fixture = "needs-pkgs"

    def setUp(self):
        super().setUp()
        self.sb.add_plugin("fake-packages.lua")
        self.sb.write("repro.lua", 'rp.option("packages.default_backend", "fake")\n'
                      'rp.profile { name = "main", default = true, use = { "a", "b", "c" } }\n')
        self.sb.write("modules/dev/c++.lua", "return rp.module { }")
        self.sb.write("modules/a.lua", 'return rp.module { packages = { "git", "vim" } }')
        self.sb.write("modules/b.lua", 'return rp.module { packages = { "vim", "curl" } }')
        # c's packages must wait for cfg's file, which comes after a's batch.
        self.sb.write("modules/cfg.lua",
                      'return rp.module { files = { { dest = "~/.cfgrc", content = "x\\n" } } }')
        self.sb.write("modules/c.lua", 'return rp.module { requires = { "cfg" }, packages = { "zsh" } }')

    def managed_packages(self):
        db = sqlite3.connect(self.sb.home / ".local/state/repro/repro.db")
        try:
            return db.execute("SELECT name, module FROM managed_packages ORDER BY name").fetchall()
        finally:
            db.close()

    def test_one_install_per_batch_in_dependency_order(self):
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertIn("+ packages fake: install git, vim, curl  (for a, b)", r.out)
        self.assertIn("+ packages fake: install zsh\n", r.out)
        self.assertRegex(r.out, r"\n  b\s+up to date\n")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("ok    a/packages/fake", r.out)
        self.assertNotIn("b/packages", r.out)
        self.assertLess(r.out.index("ok    cfg/files/~/.cfgrc"), r.out.index("ok    c/packages/fake"))
        self.assertEqual(self.managed_packages(),
                         [("curl", "b"), ("git", "a"), ("vim", "a"), ("zsh", "c")])
        self.assertIn("summary: up to date", self.sb.run("sync", "-u").out)

    def test_status_counts_a_batched_package_for_its_module(self):
        self.assertOk(self.sb.run("sync", "-u"))
        mem = self.sb.home / ".local/state/repro/lua-backend-mem/fake.lua"
        self.assertIn("curl=true", mem.read_text())
        mem.write_text(mem.read_text().replace("curl=true", "curl=false"))
        out = self.sb.run("status").out
        self.assertRegex(out, r"\n\s*a\s+applied")
        self.assertRegex(out, r"\n\s*b\s+pending")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertIn("+ packages fake: install curl  (for b)", r.out)

    def test_a_failing_batch_stops_before_the_later_modules(self):
        self.sb.write("modules/b.lua", 'return rp.module { packages = { "vim", "fail-me" } }')
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_STEP", exit_code=1)
        self.assertIn("cannot install fail-me", r.err)
        self.assertTrue(r.out.rstrip().splitlines()[-1].startswith("failed: a/packages/fake:"))
        self.assertEqual(self.managed_packages(), [])


if __name__ == "__main__":
    main()
