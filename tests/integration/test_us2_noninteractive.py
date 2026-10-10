"""US2 acceptance 5: non-interactive ambiguity fails (exit 2) and lists candidates."""

from conftest import RepdTestCase, main


class NonInteractive(RepdTestCase):
    fixture = "providers"

    def test_yes_with_ambiguity(self):
        r = self.sb.run("sync", "-u", "dev/c++", "--dry-run", "--yes")
        self.assertError(r, "E_AMBIGUOUS")
        self.assertIn("interface dev/ide (required by dev/c++)", r.err)
        self.assertIn("dev/ide/vim, dev/ide/emacs", r.err)
        self.assertIn("--provider dev/ide=dev/ide/vim", r.err)

    def test_no_tty_implies_yes(self):
        r = self.sb.run("sync", "-u", "dev/c++")  # stdin is not a terminal
        self.assertError(r, "E_AMBIGUOUS")
        self.assertFalse((self.sb.config / "repd.lock.lua").exists())

    def test_single_provider_is_automatic_and_recorded(self):
        (self.sb.config / "modules/dev/ide/emacs.lua").unlink()
        r = self.sb.run("sync", "-u", "dev/c++")
        self.assertOk(r)
        self.assertIn("dev/ide = dev/ide/vim (only-option)", r.out)
        self.assertIn('source = "only-option"', (self.sb.config / "repd.lock.lua").read_text())

    def test_interface_without_providers(self):
        (self.sb.config / "modules/dev/ide/emacs.lua").unlink()
        (self.sb.config / "modules/dev/ide/vim.lua").unlink()
        r = self.sb.run("sync", "-u", "dev/c++")
        self.assertError(r, "E_NO_PROVIDER")
        self.assertIn("dev/c++", r.err)


if __name__ == "__main__":
    main()
