"""SC-004: an interrupted apply is resumable — re-running completes the work and
no step is applied twice (research R15)."""

from conftest import RepdTestCase, journal_steps, main

MODULES = {
    "modules/a.lua": 'return rp.module { files = { { dest = "~/a1", content = "1" }, { dest = "~/a2", content = "2" } } }',
    # Step k: the action interrupts repd (SIGINT to its parent) after doing its work.
    "modules/b.lua": """
return rp.module {
  requires = { "a" },
  actions = { rp.action { name = "interrupt", mode = "user",
    check = function(ctx) return ctx.fs.exists("~/b.done") end,
    run = function(ctx) ctx.exec("touch ~/b.done; kill -INT $PPID") end } },
}""",
    "modules/c.lua": 'return rp.module { requires = { "b" }, files = { { dest = "~/c1", content = "3" } }, packages = { "fake:jq" } }',
}


class Resume(RepdTestCase):
    fixture = "files-only"

    def setUp(self):
        super().setUp()
        for rel, text in MODULES.items():
            self.sb.write(rel, text)
        self.sb.write("repd.lua", 'rp.profile { name = "main", default = true, use = { "c" } }')
        self.sb.add_plugin("fake-packages.lua")

    def test_sigint_then_resume(self):
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_INTERRUPTED", exit_code=130)
        self.assertTrue(self.sb.home_path("a1").exists())
        self.assertTrue(self.sb.home_path("b.done").exists())
        self.assertFalse(self.sb.home_path("c1").exists(), "nothing runs after the interrupt")
        j = self.sb.run("journal", "--last", "1")
        self.assertIn("interrupted", j.out)
        st = self.sb.run("status").out
        self.assertRegex(st, r"b\s+applied")
        self.assertRegex(st, r"c\s+failed")  # interrupted before its first step

        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertTrue(self.sb.home_path("c1").exists())
        self.assertNotIn("ok    a/", r.out, "completed steps are not re-applied")
        self.assertNotIn("ok    b/action/interrupt", r.out)
        self.assertIn("summary: up to date", self.sb.run("sync", "-u").out)

        steps = journal_steps(self.sb.run("journal", "--last", "10").out)
        oks = [sid for status, sid in steps if status == "ok"]
        self.assertEqual(len(oks), len(set(oks)), f"a step was applied twice: {oks}")
        self.assertEqual(sorted(oks), sorted([
            "a/files/~/a1", "a/files/~/a2", "b/action/interrupt", "c/packages/fake", "c/files/~/c1"]))

    def test_failed_step_resume(self):
        self.sb.write("modules/c.lua", 'return rp.module { requires = { "b" }, packages = { "fake:fail-once" } }')
        self.sb.home_path("b.done").write_text("")  # skip the interrupt
        r = self.sb.run("sync", "-u")
        self.assertExit(r, 1)
        self.assertTrue(r.out.rstrip().splitlines()[-1].startswith("failed: c/packages/fake:"))
        self.sb.write("modules/c.lua", 'return rp.module { requires = { "b" }, packages = { "fake:jq" } }')
        self.assertOk(self.sb.run("sync", "-u"))


if __name__ == "__main__":
    main()
