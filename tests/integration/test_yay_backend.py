"""The repd-yay add-on (librepd-yay.so) against stub yay and sudo.

yay runs on the terminal: its output reaches the user directly and sudo's
password prompt is shown there. Without a terminal and without cached sudo
credentials the step fails up front instead of hanging.
"""

import os
import pty
import select
import shutil
import time

from conftest import REPD, RepdTestCase, main

YAY = r"""#!/bin/sh
S="$YAY_STUB"
echo "yay $*" >> "$S/log"
case "$1" in
  -Pg) printf '{\n\t"sudobin": "%s",\n\t"sudoflags": ""\n}\n' "${YAY_SUDOBIN:-sudo}" ;;
  -Syu) echo ":: upgrading everything"; touch "$S/upgraded" ;;
  -S|-Sy)
    while [ "$1" != "--" ]; do shift; done; shift
    echo ":: building $*"
    for p; do
      [ "$p" = "broken-git" ] && { echo "==> ERROR: A failure occurred in build()." >&2; exit 1; }
      echo "$p" >> "$S/installed"
    done ;;
esac
"""

PACMAN = r"""#!/bin/sh
S="$YAY_STUB"
echo "pacman $*" >> "$S/log"
[ "$1" = "-T" ] || exit 1
shift; [ "$1" = "--" ] && shift; rc=0
for p; do
  [ "$p" = "yay" ] && grep -qx "yay-bin" "$S/installed" 2>/dev/null && continue  # provides
  grep -qx "$p" "$S/installed" 2>/dev/null || { echo "$p"; rc=127; }
done
exit $rc
"""

# Real sudo prompts on /dev/tty; the stub uses stderr/stdin, which are the same
# terminal when repd runs on one.
SUDO = r"""#!/bin/sh
S="$YAY_STUB"
echo "sudo $*" >> "$S/log"
case "$1" in
  -n) [ -f "$S/cached" ]; exit ;;
  -v)
    printf '%s' "$3" | sed "s/%u/$USER/" >&2
    read -r pw || exit 1
    [ "$pw" = "hunter2" ] || { echo "Sorry, try again." >&2; exit 1; }
    touch "$S/cached" ;;
esac
"""

PROFILE = 'rp.profile { name = "main", default = true, use = { "aur" } }\n'
# Most tests skip the PKGBUILD review so they can run without a terminal.
REPD_LUA = 'rp.option("packages.default_backend", "yay")\nrp.option("yay.review", "skip")\n' + PROFILE

YAY_ARGS = "--needed --noconfirm --answerclean None --answerdiff None --answeredit None --sudoloop --"
REVIEW_ARGS = "--needed --diffmenu --noanswerdiff --answerclean None --answeredit None --sudoloop --"


class Yay(RepdTestCase):
    def setUp(self):
        super().setUp()
        plugin = os.environ.get("REPD_YAY_PLUGIN")
        if not plugin:
            self.skipTest("built with -Daddons=disabled")
        shutil.copy(plugin, self.sb.plugin_dir() / "librepd-yay.so")
        self.state = self.sb.tmp / "yay"
        bin_dir = self.sb.tmp / "bin"
        self.state.mkdir()
        bin_dir.mkdir()
        for name, text in (("yay", YAY), ("sudo", SUDO), ("pacman", PACMAN)):
            p = bin_dir / name
            p.write_text(text)
            p.chmod(0o755)
        self.sb.env["PATH"] = f"{bin_dir}:{self.sb.env['PATH']}"
        self.sb.env["YAY_STUB"] = str(self.state)
        self.sb.write("repd.lua", REPD_LUA)
        self.packages("neovim", "paru-bin")

    def packages(self, *names):
        quoted = ", ".join(f'"{n}"' for n in names)
        self.sb.write("modules/aur.lua", f"return rp.module {{ packages = {{ {quoted} }} }}")

    def log(self):
        p = self.state / "log"
        return p.read_text() if p.exists() else ""

    def installed(self):
        p = self.state / "installed"
        return p.read_text().split() if p.exists() else []

    def test_cached_credentials_install_on_the_terminal(self):
        (self.state / "cached").touch()
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("packages = yay 1.0.0 (system, default)", r.out)
        self.assertIn("+ packages yay: install neovim, paru-bin", r.out)
        self.assertIn(":: building neovim paru-bin", r.out)  # yay's own output is not swallowed
        self.assertIn(f"  $ yay -S {YAY_ARGS} neovim paru-bin", r.err)
        self.assertIn("sudo -n true", self.log())
        self.assertNotIn("sudo -v", self.log())
        self.assertEqual(self.installed(), ["neovim", "paru-bin"])
        r = self.sb.run("sync", "-u")
        self.assertIn("summary: up to date", r.out)

    def test_no_terminal_and_no_credentials_fails_before_running_yay(self):
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_STEP", exit_code=1)
        self.assertIn("yay needs sudo for pacman, but sudo wants a password and repd is not running in a terminal",
                      r.err)
        self.assertIn("hint: run `repd sync -u` in a terminal, or authenticate first with `sudo -v`", r.err)
        self.assertNotIn("yay -S", self.log())
        self.assertEqual(self.installed(), [])

    def test_provided_packages_count_as_installed(self):
        (self.state / "installed").write_text("yay-bin\n")
        self.packages("yay")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("summary: up to date", r.out)
        self.assertIn("pacman -T -- yay", self.log())

    def test_password_prompt_is_shown_on_the_terminal(self):
        code, transcript = self.run_on_terminal("hunter2")
        self.assertEqual(code, 0, transcript)
        user = self.sb.env["USER"]
        self.assertIn(f"[repd] sudo password for {user} (yay installs packages): ", transcript)
        self.assertIn(":: building neovim paru-bin", transcript)
        self.assertEqual(self.installed(), ["neovim", "paru-bin"])

    def test_wrong_password_fails_the_step(self):
        code, transcript = self.run_on_terminal("wrong")
        self.assertEqual(code, 1, transcript)
        self.assertIn("sudo authentication failed", transcript)
        self.assertNotIn("yay -S", self.log())

    def test_build_failure_points_at_yay_output(self):
        (self.state / "cached").touch()
        self.packages("neovim", "broken-git")
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_STEP", exit_code=1)
        self.assertIn("==> ERROR: A failure occurred in build().", r.err)
        self.assertIn("packages backend yay: install yay -S failed (exit 1)", r.err)
        self.assertIn("hint: yay's output is shown above", r.err)

    def test_other_privilege_tool_is_left_to_yay(self):
        self.sb.env["YAY_SUDOBIN"] = "doas"
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertFalse([l for l in self.log().splitlines() if l.startswith("sudo ")])
        self.assertEqual(self.installed(), ["neovim", "paru-bin"])

    def test_packages_belong_to_the_user_pass(self):
        r = self.sb.run("sync", "-s", "--dry-run", root=True)
        self.assertOk(r)
        self.assertIn("· packages yay: install neovim, paru-bin  [user pass]", r.out)

    def test_upgrade_is_a_full_yay_upgrade_before_installs(self):
        (self.state / "cached").touch()
        r = self.sb.run("sync", "-u", "--upgrade")
        self.assertOk(r)
        self.assertIn("~ upgrade yay: upgrade all installed packages", r.out)
        lines = self.log().splitlines()
        upgrade = lines.index("yay -Syu --noconfirm --answerclean None --answerdiff None --answeredit None --sudoloop")
        install = next(i for i, l in enumerate(lines) if l.startswith("yay -S --needed"))
        self.assertLess(upgrade, install)
        self.assertNotIn("yay -Sy ", self.log())  # never a refresh without an upgrade

    def test_upgrade_with_modules_rebuilds_without_a_refresh(self):
        (self.state / "cached").touch()
        (self.state / "installed").write_text("neovim\n")
        r = self.sb.run("sync", "-u", "--upgrade", "aur")
        self.assertOk(r)
        self.assertIn("~ upgrade yay: upgrade neovim", r.out)
        lines = self.log().splitlines()
        self.assertIn(f"yay -S {YAY_ARGS} neovim", lines)  # AUR: rebuilt if newer; repo: no partial upgrade
        self.assertIn(f"yay -S {YAY_ARGS} paru-bin", lines)
        self.assertFalse([l for l in lines if l.startswith("yay -Sy")])

    # ---- review (yay.review) and --option ----

    def test_review_is_the_default_and_needs_a_terminal(self):
        (self.state / "cached").touch()
        self.sb.write("repd.lua", 'rp.option("packages.default_backend", "yay")\n' + PROFILE)
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_STEP", exit_code=1)
        self.assertIn('yay.review is "diff": yay shows AUR build scripts and asks before building', r.err)
        self.assertIn("hint: run `repd sync -u` in a terminal, or skip the review for this run: "
                      "--option yay.review=skip", r.err)
        self.assertNotIn("yay -S", self.log())

    def test_review_runs_yay_with_its_diff_menu_on_the_terminal(self):
        (self.state / "cached").touch()
        self.sb.write("repd.lua", 'rp.option("packages.default_backend", "yay")\n' + PROFILE)
        code, transcript = self.run_on_terminal(None)
        self.assertEqual(code, 0, transcript)
        self.assertIn(f"yay -S {REVIEW_ARGS} neovim paru-bin", self.log())
        self.assertNotIn("--noconfirm", self.log())

    def test_option_flag_overrides_repd_lua(self):
        (self.state / "cached").touch()
        self.sb.write("repd.lua", 'rp.option("yay.review", "diff")\n' + PROFILE)
        r = self.sb.run("sync", "-u", "--option", "packages.default_backend=yay", "--option", "yay.review=skip")
        self.assertOk(r)
        self.assertIn(f"yay -S {YAY_ARGS} neovim paru-bin", self.log())

    def test_option_flag_errors(self):
        r = self.sb.run("sync", "-u", "--option", "yay.review=maybe")
        self.assertError(r, "E_CONFIG")
        self.assertIn("--option: option yay.review cannot be 'maybe'", r.err)
        self.assertIn("use one of: diff, skip (default diff)", r.err)
        r = self.sb.run("sync", "-u", "--option", "yay.reveiw=skip")
        self.assertError(r, "E_CONFIG")
        self.assertIn("unknown option 'yay.reveiw'", r.err)
        self.assertIn("did you mean 'yay.review'?", r.err)
        r = self.sb.run("sync", "-u", "--option", "yay.review")
        self.assertError(r, "E_USAGE")
        self.assertIn("--option expects KEY=VALUE", r.err)
        r = self.sb.run("sync", "-u", "--option", "packages.default_backend=auto")
        self.assertError(r, "E_CONFIG")
        r = self.sb.run("sync", "-u", "--option", "packages.backend=yay")
        self.assertError(r, "E_CONFIG")
        self.assertIn("packages.backend was renamed to packages.default_backend", r.err)
        self.assertIn("--option packages.default_backend=yay", r.err)

    def test_repd_lua_options_of_absent_addons_are_ignored(self):
        (self.state / "cached").touch()
        self.sb.write("repd.lua", REPD_LUA + 'rp.option("flatpak.scope", "user")\n')
        self.assertOk(self.sb.run("sync", "-u"))
        self.sb.write("repd.lua", REPD_LUA + 'rp.option("yay.reveiw", "skip")\n')
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_CONFIG")
        self.assertIn("repd.lua: unknown option 'yay.reveiw'", r.err)

    def run_on_terminal(self, password, timeout=30):
        """Runs `repd sync -u` on a pseudo-terminal, answering sudo's prompt."""
        pid, fd = pty.fork()
        if pid == 0:  # child
            os.chdir(self.sb.tmp)
            os.execve(REPD, [REPD, "sync", "-u"], self.sb.env_for())
        transcript, answered = b"", False
        deadline = time.time() + timeout
        while time.time() < deadline:
            r, _, _ = select.select([fd], [], [], 0.2)
            if fd in r:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    break
                if not chunk:
                    break
                transcript += chunk
                if password is not None and not answered and b"(yay installs packages): " in transcript:
                    os.write(fd, password.encode() + b"\n")
                    answered = True
        _, status = os.waitpid(pid, 0)
        os.close(fd)
        return os.waitstatus_to_exitcode(status), transcript.decode(errors="replace")


if __name__ == "__main__":
    main()
