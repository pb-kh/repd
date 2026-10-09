"""US1 acceptance 5: drift on managed files is restored without prompting."""

import os

from conftest import ReproTestCase, main


class Drift(ReproTestCase):
    fixture = "files-only"

    def test_content_drift_restored(self):
        self.assertOk(self.sb.run("sync", "-u"))
        inputrc = self.sb.home_path(".inputrc")
        inputrc.write_text("tampered\n")
        r = self.sb.run("sync", "-u", "--yes")
        self.assertOk(r)
        self.assertIn("~ files   ~/.inputrc  restore drift", r.out)
        self.assertEqual(inputrc.read_text(), "set editing-mode vi\n")

    def test_link_replaced_and_deleted_file_restored(self):
        self.assertOk(self.sb.run("sync", "-u"))
        link = self.sb.home_path(".gitconfig")
        self.assertTrue(link.is_symlink())
        link.unlink()
        link.write_text("not a link\n")
        self.sb.home_path(".inputrc").unlink()
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("~ files   ~/.gitconfig  restore drift", r.out)
        self.assertIn("+ files   ~/.inputrc  create", r.out)
        self.assertTrue(link.is_symlink())
        self.assertEqual(os.readlink(link), str(self.sb.config / "modules/dotfiles/files/gitconfig"))

    def test_deleted_state_store_never_skips(self):
        """Edge case: losing bookkeeping must not make repro skip real work."""
        self.assertOk(self.sb.run("sync", "-u"))
        state = self.sb.home / ".local/state/repro"
        for p in state.glob("repro.db*"):
            p.unlink()
        self.sb.home_path(".inputrc").write_text("x\n")
        r = self.sb.run("sync", "-u")
        # Without the ledger the changed file is unknown, so it is refused rather than overwritten.
        self.assertError(r, "E_BLOCKED", exit_code=1)
        self.assertOk(self.sb.run("sync", "-u", "--adopt"))
        self.assertEqual(self.sb.home_path(".inputrc").read_text(), "set editing-mode vi\n")


if __name__ == "__main__":
    main()
