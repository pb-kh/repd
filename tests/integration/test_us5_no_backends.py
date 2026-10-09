"""US5 acceptance 1: files/env work with zero backends installed."""

from conftest import ReproTestCase, main


class NoBackends(ReproTestCase):
    fixture = "files-only"

    def test_files_only_repo_succeeds(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertNotIn("backends:", r.out)
        self.assertTrue(self.sb.home_path(".gitconfig").is_symlink())
        r = self.sb.run("plugins", "list")
        self.assertOk(r)
        self.assertEqual(r.out.strip(), "NAMESPACE  NAME  VERSION  ABI  ORIGIN  PATH")


if __name__ == "__main__":
    main()
