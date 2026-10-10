"""The repd-pacman add-on (librepd-pacman.so) against a stub pacman.

Installs never refresh the databases. `--upgrade` alone is a full `-Syu`;
`--upgrade MODULE…` upgrades single packages after a refresh, a partial
upgrade, so it needs pacman.partial = "allow".
"""

import os
import shutil

from conftest import RepdTestCase, main

PACMAN = r"""#!/bin/sh
S="$PACMAN_STUB"
echo "pacman $*" >> "$S/log"
case "$1" in
  -T)
    shift; [ "$1" = "--" ] && shift; rc=0
    for p; do grep -qx "$p" "$S/installed" 2>/dev/null || { echo "$p"; rc=127; }; done
    exit $rc ;;
  -Syu) echo ":: Starting full system upgrade..." ;;
  -S|-Sy)
    while [ "$1" != "--" ]; do shift; done; shift
    for p; do grep -qx "$p" "$S/installed" 2>/dev/null || echo "$p" >> "$S/installed"; done ;;
esac
"""

PROFILE = 'rp.profile { name = "main", default = true, use = { "dev/rust", "base" } }\n'


class Pacman(RepdTestCase):
    def setUp(self):
        super().setUp()
        plugin = os.environ.get("REPD_PACMAN_PLUGIN")
        if not plugin:
            self.skipTest("built with -Daddons=disabled")
        shutil.copy(plugin, self.sb.plugin_dir() / "librepd-pacman.so")
        self.state = self.sb.tmp / "pacman"
        bin_dir = self.sb.tmp / "bin"
        self.state.mkdir()
        bin_dir.mkdir()
        stub = bin_dir / "pacman"
        stub.write_text(PACMAN)
        stub.chmod(0o755)
        self.sb.env["PATH"] = f"{bin_dir}:{self.sb.env['PATH']}"
        self.sb.env["PACMAN_STUB"] = str(self.state)
        self.sb.write("repd.lua", 'rp.option("packages.default_backend", "pacman")\n' + PROFILE)
        self.sb.write("modules/dev/rust.lua", 'return rp.module { packages = { "rustup", "sccache" } }')
        self.sb.write("modules/base.lua", 'return rp.module { packages = { "git" } }')
        (self.state / "installed").write_text("rustup\ngit\n")

    def log(self):
        p = self.state / "log"
        return p.read_text().splitlines() if p.exists() else []

    def test_installs_never_refresh(self):
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("pacman -S --needed --noconfirm -- sccache", self.log())
        self.assertFalse([l for l in self.log() if l.startswith("pacman -Sy")])

    def test_upgrade_is_a_full_upgrade(self):
        r = self.sb.run("sync", "-s", "--upgrade", root=True)
        self.assertOk(r)
        self.assertIn("~ upgrade pacman: upgrade all installed packages", r.out)
        self.assertLess(self.log().index("pacman -Syu --noconfirm"),
                        self.log().index("pacman -S --needed --noconfirm -- sccache"))

    def test_upgrade_with_modules_is_refused_by_default(self):
        r = self.sb.run("sync", "-s", "--upgrade", "dev/rust", root=True)
        self.assertOk(r)
        self.assertIn('warning: --upgrade: pacman: pacman.partial is "refuse": refreshing the databases to '
                      "upgrade single packages is a partial upgrade; skipped rustup, sccache", r.err)
        self.assertIn("hint: upgrade everything (repd sync --upgrade, without modules), or allow it: "
                      "--option pacman.partial=allow", r.err)
        self.assertNotIn("upgrade pacman", r.out)
        self.assertFalse([l for l in self.log() if l.startswith("pacman -Sy")])

    def test_upgrade_with_modules_when_partial_upgrades_are_allowed(self):
        r = self.sb.run("sync", "-s", "--upgrade", "dev/rust", "--option", "pacman.partial=allow", root=True)
        self.assertOk(r)
        self.assertIn("~ upgrade pacman: upgrade rustup", r.out)
        # one refresh with only the named module's installed packages, before installs; base is not named
        refreshes = [l for l in self.log() if l.startswith("pacman -Sy")]
        self.assertEqual(refreshes, ["pacman -Sy --needed --noconfirm -- rustup"])
        self.assertLess(self.log().index(refreshes[0]),
                        self.log().index("pacman -S --needed --noconfirm -- sccache"))

    def test_partial_option_in_repd_lua(self):
        self.sb.write("repd.lua", 'rp.option("packages.default_backend", "pacman")\n'
                      'rp.option("pacman.partial", "allow")\n' + PROFILE)
        r = self.sb.run("sync", "-s", "--upgrade", "dev/rust", "base", "--dry-run", root=True)
        self.assertOk(r)
        self.assertIn("~ upgrade pacman: upgrade git, rustup", r.out)


if __name__ == "__main__":
    main()
