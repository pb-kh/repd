"""Environment variables without assuming an init system: per-shell drop-ins
(sh, fish) and systemd's environment.d, selected by rp.option("env.targets")."""

import shutil
import subprocess

from conftest import ReproTestCase, main

MODULE = """return rp.module {
  env = { GREETING = "it's a \\\\ test", EDITOR = "vi" },
  system_env = { SITE = "lab" },
}"""


class EnvTargets(ReproTestCase):
    fixture = "files-only"

    def setUp(self):
        super().setUp()
        self.sb.write("modules/dotfiles/init.lua", MODULE)

    def login_shell(self, shell):
        etc = self.sb.sys_path("/etc")
        etc.mkdir(parents=True, exist_ok=True)
        (etc / "passwd").write_text(f"{self.sb.env['USER']}:x:1000:1000::{self.sb.home}:{shell}\n")

    def test_auto_sh_profile_is_sourced_by_a_login_shell(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("~/.profile  add repro block (load ~/.local/share/repro/env.d/*.sh)", r.out)
        out = subprocess.run(["sh", "-c", '. "$HOME/.profile"; printf "%s|%s" "$GREETING" "$EDITOR"'],
                             env=self.sb.env_for(), capture_output=True, text=True).stdout
        self.assertEqual(out, "it's a \\ test|vi")
        self.assertIn("summary: 0 changes in this pass", self.sb.run("sync", "-u").out)

    def test_existing_profile_content_is_kept(self):
        self.sb.home_path(".profile").write_text("export KEEP=1\n")
        self.assertOk(self.sb.run("sync", "-u"))
        text = self.sb.home_path(".profile").read_text()
        self.assertTrue(text.startswith("export KEEP=1\n# BEGIN repro\n"), text)
        self.assertEqual(text.count("# BEGIN repro"), 1)

    def test_bash_with_bash_profile_and_zsh(self):
        self.login_shell("/bin/bash")
        self.sb.home_path(".bash_profile").write_text("")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertIn("~/.bash_profile  add repro block", r.out)
        self.login_shell("/usr/bin/zsh")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertIn("~/.zprofile  add repro block", r.out)

    def test_auto_fish_login_shell(self):
        self.login_shell("/usr/bin/fish")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertNotIn(".profile", r.out)
        conf = self.sb.home_path(".config/fish/conf.d/repro-dotfiles.fish")
        self.assertIn("set -gx GREETING 'it\\'s a \\\\ test'", conf.read_text())
        if shutil.which("fish"):
            out = subprocess.run(["fish", "--no-config", "-c", f"source {conf}; printf '%s|%s' $GREETING $EDITOR"],
                                 env=self.sb.env_for(), capture_output=True, text=True).stdout
            self.assertEqual(out, "it's a \\ test|vi")

    def test_auto_adds_environment_d_under_systemd(self):
        self.sb.sys_path("/run/systemd/system").mkdir(parents=True)
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertIn("(~/.config/environment.d/90-repro-dotfiles.conf)", r.out)
        self.assertIn("(~/.local/share/repro/env.d/dotfiles.sh)", r.out)
        self.assertIn("(/etc/environment.d/90-repro-dotfiles.conf)  [system pass]", r.out)

    def test_system_targets(self):
        self.sb.sys_path("/etc/fish").mkdir(parents=True)
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("export SITE='lab'", self.sb.sys_path("/etc/profile.d/repro-dotfiles.sh").read_text())
        self.assertIn("set -gx SITE 'lab'", self.sb.sys_path("/etc/fish/conf.d/repro-dotfiles.fish").read_text())

    def test_explicit_targets(self):
        self.sb.write("repro.lua", (self.sb.config / "repro.lua").read_text()
                      + 'rp.option("env.targets", { "fish", "environment.d" })\n')
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertIn("(~/.config/fish/conf.d/repro-dotfiles.fish)", r.out)
        self.assertIn("(~/.config/environment.d/90-repro-dotfiles.conf)", r.out)
        self.assertNotIn(".profile", r.out)

    def test_unknown_target(self):
        self.sb.write("repro.lua", 'rp.option("env.targets", { "nushell" })\n')
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_CONFIG")
        self.assertIn("sh, fish, environment.d", r.err + r.out)


if __name__ == "__main__":
    main()
