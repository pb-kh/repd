"""US2 acceptance 6: a dependency cycle is a plan-time error that shows the chain."""

from conftest import ReproTestCase, main


class Cycles(ReproTestCase):
    fixture = "providers"

    def test_cycle_chain(self):
        self.sb.write("modules/a.lua", 'return rp.module { requires = { "b" } }')
        self.sb.write("modules/b.lua", 'return rp.module { requires = { "c" } }')
        self.sb.write("modules/c.lua", 'return rp.module { requires = { "a" } }')
        managed = lambda: {k: v for k, v in self.sb.snapshot(self.sb.home).items()
                           if not k.startswith(".local")}
        before = managed()
        r = self.sb.run("sync", "-u", "a")
        self.assertError(r, "E_CYCLE")
        self.assertIn("a → b → c → a", r.err)
        self.assertEqual(managed(), before, "no mutation before the plan succeeds")
        j = self.sb.run("journal")
        self.assertRegex(j.out, r"run #1  user .* plan-error")

    def test_cycle_through_interface(self):
        self.sb.write("modules/dev/ide/vim.lua",
                      'return rp.module { provides = "dev/ide", priority = 10, requires = { "dev/c++" } }')
        r = self.sb.run("sync", "-u", "dev/c++", "--provider", "dev/ide=dev/ide/vim")
        self.assertError(r, "E_CYCLE")
        self.assertIn("dev/c++ → dev/ide/vim → dev/c++", r.err)


if __name__ == "__main__":
    main()
