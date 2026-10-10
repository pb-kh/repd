"""The repd-openrc add-on (librepd-openrc.so) against stub rc-update/rc-service.

The stubs keep runlevel membership and started services in files under the
sandbox, separately for system and --user scope, and log every invocation.
"""

import os
import shutil

from conftest import RepdTestCase, main

RC_UPDATE = r"""#!/bin/sh
S="$RC_STUB"; scope=system; all=
echo "rc-update $*" >> "$S/log"
args=
for a in "$@"; do
  case "$a" in --user) scope=user ;; --nocolor) ;; --all) all=1 ;; *) args="$args $a" ;; esac
done
set -- $args
touch "$S/$scope.runlevels"
case "$1" in
  show) while read -r n l; do printf '%20s | %s\n' "$n" "$l"; done < "$S/$scope.runlevels" ;;
  add)
    grep -qx "$2" "$S/$scope.services" 2>/dev/null || { echo " * rc-update: service \`$2' does not exist" >&2; exit 1; }
    echo "$2 $3" >> "$S/$scope.runlevels" ;;
  del)
    [ -n "$all" ] || { echo "stub: expected --all" >&2; exit 1; }
    grep -v "^$2 " "$S/$scope.runlevels" > "$S/tmp"; mv "$S/tmp" "$S/$scope.runlevels" ;;
esac
"""

RC_SERVICE = r"""#!/bin/sh
S="$RC_STUB"; scope=system; exists=
echo "rc-service $*" >> "$S/log"
args=
for a in "$@"; do
  case "$a" in --user) scope=user ;; --nocolor) ;; --exists) exists=1 ;; *) args="$args $a" ;; esac
done
set -- $args
touch "$S/$scope.started"
if [ -n "$exists" ]; then grep -qx "$1" "$S/$scope.services" 2>/dev/null; exit; fi
case "$2" in
  status) grep -qx "$1" "$S/$scope.started" && { echo " * status: started"; exit 0; }
          echo " * status: stopped"; exit 3 ;;
  start) echo "$1" >> "$S/$scope.started" ;;
  stop) grep -vx "$1" "$S/$scope.started" > "$S/tmp"; mv "$S/tmp" "$S/$scope.started" ;;
esac
"""

REPD_LUA = """rp.option("services.backend", "openrc")
rp.profile { name = "main", default = true, use = { "svc" } }
"""


class OpenRC(RepdTestCase):
    def setUp(self):
        super().setUp()
        plugin = os.environ.get("REPD_OPENRC_PLUGIN")
        if not plugin:
            self.skipTest("built with -Daddons=disabled")
        shutil.copy(plugin, self.sb.plugin_dir() / "librepd-openrc.so")
        self.rc = self.sb.tmp / "rc"
        bin_dir = self.sb.tmp / "bin"
        self.rc.mkdir()
        bin_dir.mkdir()
        for name, text in (("rc-update", RC_UPDATE), ("rc-service", RC_SERVICE)):
            p = bin_dir / name
            p.write_text(text)
            p.chmod(0o755)
        self.sb.env["PATH"] = f"{bin_dir}:{self.sb.env['PATH']}"
        self.sb.env["RC_STUB"] = str(self.rc)
        self.sb.write("repd.lua", REPD_LUA)
        self.services("system", "sshd", "elogind", "cronie")
        self.services("user", "pipewire")

    def services(self, scope, *names):
        with open(self.rc / f"{scope}.services", "a") as f:
            f.writelines(n + "\n" for n in names)

    def state(self, scope, kind):
        p = self.rc / f"{scope}.{kind}"
        return p.read_text() if p.exists() else ""

    def log(self):
        p = self.rc / "log"
        return p.read_text() if p.exists() else ""

    def module(self, services):
        self.sb.write("modules/svc.lua", f"return rp.module {{ services = {{ {services} }} }}")

    def test_listed_as_native_backend(self):
        self.module('{ unit = "sshd", state = "enabled" }')
        r = self.sb.run("plugins", "list")
        self.assertOk(r)
        self.assertRegex(r.out, r"services\s+openrc\s+1\.0\.0\s+1\s+system\s+.*librepd-openrc\.so")

    def test_user_service_enabled_and_started(self):
        self.module('{ unit = "pipewire", scope = "user", state = "enabled", active = true }')
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("services = openrc 1.0.0 (system, option)", r.out)
        self.assertIn("pipewire (user)  enable, start", r.out)
        self.assertIn("rc-update --user add pipewire default", self.log())
        self.assertIn("rc-service --user --nocolor pipewire start", self.log())
        self.assertEqual(self.state("user", "runlevels"), "pipewire default\n")
        self.assertEqual(self.state("user", "started"), "pipewire\n")
        self.assertEqual(self.state("system", "runlevels"), "")
        r = self.sb.run("sync", "-u")
        self.assertIn("summary: up to date", r.out)

    def test_system_service_with_systemd_name(self):
        self.module('{ unit = "sshd.service", state = "enabled" }')
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("rc-update add sshd default", self.log())
        self.assertNotIn("--user", self.log())
        self.assertEqual(self.state("system", "runlevels"), "sshd default\n")

    def test_any_runlevel_counts_as_enabled(self):
        (self.rc / "system.runlevels").write_text("elogind boot\n")
        self.module('{ unit = "elogind", state = "enabled" }')
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("summary: up to date", r.out)
        self.assertNotIn(" add ", self.log())

    def test_disable_removes_from_all_runlevels_and_stops(self):
        (self.rc / "system.runlevels").write_text("cronie boot\ncronie default\nsshd default\n")
        (self.rc / "system.started").write_text("cronie\n")
        self.module('{ unit = "cronie", state = "disabled", active = false }')
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("cronie  disable, stop", r.out)
        self.assertIn("rc-update --all del cronie", self.log())
        self.assertEqual(self.state("system", "runlevels"), "sshd default\n")
        self.assertEqual(self.state("system", "started"), "")

    def test_unknown_service_fails_with_tool_message(self):
        self.module('{ unit = "nosuchd", state = "enabled" }')
        r = self.sb.run("sync", "-s", root=True)
        self.assertError(r, "E_STEP", exit_code=1)
        self.assertIn("rc-update add nosuchd failed (exit 1)", r.err)
        self.assertIn("does not exist", r.err)

    def test_systemd_only_unit_type_is_refused(self):
        self.module('{ unit = "fstrim.timer", state = "enabled" }')
        r = self.sb.run("sync", "-s", root=True)
        self.assertNotEqual(r.code, 0, repr(r))
        self.assertIn("fstrim.timer is a systemd unit type that OpenRC has no equivalent for", r.err)
        self.assertEqual(self.log(), "")

    def test_missing_backend_hint_names_openrc(self):
        os.remove(self.sb.plugin_dir() / "librepd-openrc.so")
        self.sb.sys_path("/run/openrc").mkdir(parents=True)
        self.sb.write("repd.lua", REPD_LUA.replace('rp.option("services.backend", "openrc")\n', ""))
        self.module('{ unit = "sshd", state = "enabled" }')
        r = self.sb.run("sync", "-s", "--dry-run", root=True)
        self.assertError(r, "E_NO_BACKEND")
        self.assertIn("repd-openrc", r.err)
        self.assertNotIn("repd-systemd", r.err)


if __name__ == "__main__":
    main()
