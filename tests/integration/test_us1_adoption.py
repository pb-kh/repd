"""US1 acceptance 4 / FR-016: unmanaged existing files are refused unless adopted."""

from conftest import ReproTestCase, journal_steps, main

MODULE = """
return rp.module {
  files = {
    { dest = "~/.bashrc", content = "# managed by repro\\n" },
    { dest = "~/.profile", content = "# same\\n" },
  },
}
"""


class Adoption(ReproTestCase):
    fixture = "files-only"

    def setUp(self):
        super().setUp()
        self.sb.write("modules/dotfiles/init.lua", MODULE)
        self.sb.home_path(".bashrc").write_text("mine\n")

    def test_refusal_names_path_and_suggests_adopt(self):
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_BLOCKED", exit_code=1)
        self.assertIn("! files   ~/.bashrc  exists and is not managed by repro — re-run with --adopt", r.out)
        self.assertIn("~/.bashrc", r.err)
        self.assertIn("--adopt", r.err)
        self.assertTrue(r.out.rstrip().splitlines()[-1].startswith("failed: dotfiles/files/~/.bashrc:"))
        self.assertEqual(self.sb.home_path(".bashrc").read_text(), "mine\n")
        # The run stopped before the module's first mutation.
        self.assertFalse(self.sb.home_path(".profile").exists())

    def test_adopt_flag_backs_up_and_journals(self):
        r = self.sb.run("sync", "-u", "--adopt")
        self.assertOk(r)
        self.assertIn("adopt existing file (backup kept)", r.out)
        self.assertEqual(self.sb.home_path(".bashrc").read_text(), "# managed by repro\n")
        backups = list(self.sb.home.glob(".bashrc.repro-adopted-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_text(), "mine\n")
        j = self.sb.run("journal")
        self.assertIn("(adopted, backup: ~/.bashrc.repro-adopted-", j.out)
        # Now it is managed: a plain re-run is up to date, drift is restored.
        self.assertIn("summary: up to date", self.sb.run("sync", "-u").out)

    def test_adopt_true_on_entry(self):
        self.sb.write("modules/dotfiles/init.lua", MODULE.replace(
            'content = "# managed by repro\\n" }', 'content = "# managed by repro\\n", adopt = true }'))
        self.assertOk(self.sb.run("sync", "-u"))

    def test_identical_unmanaged_file_is_taken_silently(self):
        self.sb.home_path(".bashrc").write_text("# managed by repro\n")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertNotIn(".bashrc", r.out)
        self.assertEqual(list(self.sb.home.glob(".bashrc.repro-adopted-*")), [])
        # Recorded as managed: later drift is restored without --adopt.
        self.sb.home_path(".bashrc").write_text("drift\n")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("~/.bashrc  restore drift", r.out)


if __name__ == "__main__":
    main()
