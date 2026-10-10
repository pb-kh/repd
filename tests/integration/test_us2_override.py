"""US2 acceptance 4: an explicit --provider override wins and is recorded as source=cli."""

from conftest import RepdTestCase, main


class Override(RepdTestCase):
    fixture = "providers"

    def test_cli_override_recorded(self):
        code, out, _ = self.sb.run_tty("sync", "-u", "dev/c++")  # prompt -> vim
        self.assertEqual(code, 0, out)
        r = self.sb.run("sync", "-u", "dev/c++", "--provider", "dev/ide=dev/ide/emacs")
        self.assertOk(r)
        self.assertIn("dev/ide = dev/ide/emacs (cli)", r.out)
        lock = (self.sb.config / "repd.lock.lua").read_text()
        self.assertIn('provider = "dev/ide/emacs"', lock)
        self.assertIn('source = "cli"', lock)
        r = self.sb.run("sync", "-u", "dev/c++", "--yes")
        self.assertIn("dev/ide = dev/ide/emacs (lock)", r.out)

    def test_invalid_override(self):
        r = self.sb.run("sync", "-u", "dev/c++", "--provider", "dev/ide=dev/base")
        self.assertError(r, "E_PROVIDER")
        self.assertIn("dev/base does not provide it", r.err)
        self.assertIn("dev/ide/vim", r.err)
        r = self.sb.run("sync", "-u", "dev/c++", "--provider", "nonsense")
        self.assertError(r, "E_USAGE")

    def test_reselect_ignores_lock(self):
        self.assertOk(self.sb.run("sync", "-u", "dev/c++", "--provider", "dev/ide=dev/ide/vim"))
        r = self.sb.run("sync", "-u", "dev/c++", "--reselect", "dev/ide", "--yes")
        self.assertError(r, "E_AMBIGUOUS")
        code, out, prompts = self.sb.run_tty("sync", "-u", "dev/c++", "--reselect", "dev/ide", answers=["2"])
        self.assertEqual(code, 0, out)
        self.assertEqual(prompts, 1)
        self.assertIn('source = "prompt"', (self.sb.config / "repd.lock.lua").read_text())

    def test_stale_lock_entry_warns_and_reresolves(self):
        self.sb.write("repd.lock.lua", 'return { version = 1, providers = { ["dev/ide"] = '
                      '{ provider = "dev/ide/gone", source = "prompt", decided_at = "2026-01-01T00:00:00Z" } } }\n')
        r = self.sb.run("sync", "-u", "dev/c++", "--yes")
        self.assertError(r, "E_AMBIGUOUS")
        self.assertIn("warning: repd.lock.lua records dev/ide/gone for dev/ide", r.err)

    def test_pin_in_repd_lua(self):
        self.sb.write("repd.lua", 'rp.pin("dev/ide", "dev/ide/emacs")\n')
        r = self.sb.run("sync", "-u", "dev/c++", "--yes")
        self.assertOk(r)
        self.assertIn("dev/ide = dev/ide/emacs (pin)", r.out)
        self.assertFalse((self.sb.config / "repd.lock.lua").exists(), "pins are not copied into the lock")


if __name__ == "__main__":
    main()
