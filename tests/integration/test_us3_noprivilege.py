"""US3 acceptance 3: the system pass without privileges is refused with a sudo hint."""

from conftest import ReproTestCase, main


class NoPrivilege(ReproTestCase):
    fixture = "mixed"

    def test_system_pass_needs_root(self):
        r = self.sb.run("sync", "-s")
        self.assertError(r, "E_PRIVILEGE")
        self.assertIn("repro sync -s needs root", r.err)
        self.assertIn("sudo repro sync -s", r.err)
        self.assertFalse(self.sb.sys_path("/var/lib/repro").exists())

    def test_sync_needs_a_mode(self):
        self.assertError(self.sb.run("sync"), "E_USAGE")
        self.assertError(self.sb.run("sync", "-u", "-s"), "E_USAGE")

    def test_status_reports_unavailable_system_store(self):
        self.assertOk(self.sb.run("sync", "-s", root=True))
        # Simulate a root-only store the user cannot read.
        db = self.sb.sys_path("/var/lib/repro")
        db.chmod(0o000)
        try:
            r = self.sb.run("status")
            self.assertOk(r)
            self.assertIn("system: unavailable (run: sudo repro status -s)", r.out)
        finally:
            db.chmod(0o700)


if __name__ == "__main__":
    main()
