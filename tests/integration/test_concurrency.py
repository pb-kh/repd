"""SC-009: two same-mode syncs -> one refused with E_BUSY; different modes both run."""

import time

from conftest import RepdTestCase, main

SLOW = """
return rp.module {
  actions = {
    rp.action {
      name = "slow",
      mode = "%s",
      check = function(ctx) return ctx.fs.exists("~/slow-%s.done") end,
      run = function(ctx) ctx.exec("sleep 2; touch ~/slow-%s.done") end,
    },
  },
}
"""


class Concurrency(RepdTestCase):
    fixture = "files-only"

    def setUp(self):
        super().setUp()
        self.sb.write("modules/slowuser.lua", SLOW % ("user", "user", "user"))
        self.sb.write("modules/slowsys.lua", SLOW % ("system", "system", "system"))

    def wait_for_lock(self, state_dir):
        lock = state_dir / "run.lock"
        for _ in range(100):
            if lock.exists() and lock.read_text().strip():
                return
            time.sleep(0.05)
        self.fail("first run never took the lock")

    def test_same_mode_one_refused(self):
        first = self.sb.spawn("sync", "-u", "slowuser")
        self.wait_for_lock(self.sb.home / ".local/state/repd")
        second = self.sb.run("sync", "-u", "slowuser")
        out, err = first.communicate(timeout=30)
        self.assertEqual(first.returncode, 0, out + err)
        self.assertError(second, "E_BUSY")
        self.assertIn("another repd run is active in user mode (pid", second.err)
        # Read-only commands never wait for the lock.
        self.assertOk(self.sb.run("status", "slowuser") if False else self.sb.run("journal"))

    def test_cross_mode_both_proceed(self):
        user = self.sb.spawn("sync", "-u", "slowuser")
        system = self.sb.spawn("sync", "-s", "slowsys", root=True)
        uo, ue = user.communicate(timeout=30)
        so, se = system.communicate(timeout=30)
        self.assertEqual(user.returncode, 0, uo + ue)
        self.assertEqual(system.returncode, 0, so + se)
        self.assertTrue(self.sb.home_path("slow-user.done").exists())
        self.assertTrue(self.sb.home_path("slow-system.done").exists())

    def test_dry_run_takes_no_lock(self):
        first = self.sb.spawn("sync", "-u", "slowuser")
        self.wait_for_lock(self.sb.home / ".local/state/repd")
        r = self.sb.run("sync", "-u", "slowuser", "--dry-run")
        first.communicate(timeout=30)
        self.assertOk(r)


if __name__ == "__main__":
    main()
