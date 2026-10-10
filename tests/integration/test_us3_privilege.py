"""US3 acceptance 1, 2, 4, 5: separate user and system passes with separate state."""

from conftest import RepdTestCase, main


class TwoPasses(RepdTestCase):
    fixture = "mixed"

    def test_user_pass_reports_pending_system_work(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("ok    shell/files/~/.zshrc", r.out)
        self.assertIn("ok    shell/env/~/.local/share/repd/env.d/shell.sh", r.out)
        self.assertIn("· env     REPD_SITE=lab  (/etc/profile.d/repd-shell.sh)  [system pass]", r.out)
        self.assertIn("· users   builder  create (shell /bin/zsh; groups wheel, audio)  [system pass]", r.out)
        self.assertIn("· udev    /etc/udev/rules.d/70-keyboard.rules  create  [system pass]", r.out)
        self.assertRegex(r.out, r"summary: 3 changes in this pass, 6 pending for system pass \(run: sudo repd sync -s\)")
        self.assertRegex(r.out, r"done: 3 applied; 6 pending for system pass \(run: sudo repd sync -s\)")
        self.assertFalse(self.sb.sys_path("/etc/motd").exists())
        self.assertEqual(self.sb.stub_log(), "")

    def test_system_pass_applies_only_system_steps(self):
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertFalse(self.sb.home_path(".zshrc").exists(), "user files untouched by the system pass")
        self.assertIn("· files   ~/.zshrc  create  [user pass]", r.out)
        self.assertEqual(self.sb.sys_path("/etc/motd").read_text(), "Welcome\n")
        self.assertIn("export REPD_SITE='lab'", self.sb.sys_path("/etc/profile.d/repd-shell.sh").read_text())
        gen = self.sb.sys_path("/etc/locale.gen").read_text()
        self.assertIn("# BEGIN repd\nde_DE.UTF-8 UTF-8\nen_US.UTF-8 UTF-8\n# END repd\n", gen)
        self.assertIn("LANG=en_US.UTF-8", self.sb.sys_path("/etc/locale.conf").read_text())
        self.assertTrue(self.sb.sys_path("/etc/udev/rules.d/70-keyboard.rules").exists())
        log = self.sb.stub_log()
        self.assertIn("useradd -m -s /bin/zsh -G wheel,audio builder", log)
        self.assertEqual(log.count("locale-gen"), 1)
        self.assertEqual(log.count("udevadm control --reload"), 1)
        self.assertIn("ok    system/devices/post/udev-reload", r.out)
        self.assertIn("ok    system/locale/post/locale-gen", r.out)
        self.assertTrue(self.sb.sys_path("/var/lib/repd/repd.db").exists())
        self.assertFalse((self.sb.home / ".local/state/repd/repd.db").exists(), "separate stores")

    def test_both_passes_then_both_skip(self):
        self.assertOk(self.sb.run("sync", "-u"))
        self.assertOk(self.sb.run("sync", "-s", root=True))
        u = self.sb.run("sync", "-u")
        s = self.sb.run("sync", "-s", root=True)
        self.assertOk(u)
        self.assertOk(s)
        self.assertIn("summary: up to date", u.out)
        self.assertIn("summary: up to date", s.out)
        log = self.sb.stub_log()
        self.assertEqual(log.count("useradd"), 1, "users are created once")
        self.assertEqual(log.count("udevadm"), 1, "post-steps only run after changes")
        st = self.sb.run("status")
        self.assertRegex(st.out, r"shell\s+applied")
        self.assertIn("system: 0 pending", st.out)
        st = self.sb.run("status", "-s", root=True)
        self.assertRegex(st.out, r"system/accounts\s+applied")
        self.assertRegex(self.sb.run("journal", "-s", root=True).out, r"run #2  system")

    def test_user_groups_are_additive(self):
        self.assertOk(self.sb.run("sync", "-s", root=True))
        self.sb.write("modules/system/accounts.lua", """
return rp.module {
  users = { { name = "builder", shell = "/bin/bash", groups = { "wheel", "audio", "video" } } },
  files = { { dest = "/etc/motd", content = "Welcome\\n" } },
}""")
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("~ users   builder  set shell /bin/bash; add groups video", r.out)
        self.assertIn("usermod -a -G video builder", self.sb.stub_log())

    def test_refresh_reruns_post_steps(self):
        self.assertOk(self.sb.run("sync", "-s", root=True))
        r = self.sb.run("sync", "-s", "--refresh", "system/devices", root=True)
        self.assertOk(r)
        self.assertEqual(self.sb.stub_log().count("udevadm"), 2)

    def test_system_only_module_recorded_separately(self):
        r = self.sb.run("sync", "-s", "system/locale", root=True)
        self.assertOk(r)
        st = self.sb.run("status", "-s", root=True)
        self.assertRegex(st.out, r"system/locale\s+applied")
        j = self.sb.run("journal")
        self.assertIn("no runs recorded yet in user mode", j.out)


if __name__ == "__main__":
    main()
