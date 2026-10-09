"""The repro-flatpak add-on (librepro-flatpak.so) against a stub flatpak.

One add-on, two backends: `flatpak` is the system-wide installation (system
pass) and `flatpak-user` the per-user one (`--user`, user pass).
"""

import os
import shutil

from conftest import ReproTestCase, main

# Each installation's contents are "<application>\t<ref>" lines in $S/system or $S/user.
FLATPAK = r"""#!/bin/sh
S="$FLATPAK_STUB"
echo "flatpak $*" >> "$S/log"
op="$1"; scope="${2#--}"; shift 2
case "$op" in
  list) [ -f "$S/$scope" ] && cat "$S/$scope"; exit 0 ;;
  update) echo "Updating $scope installation"; exit 0 ;;
esac
while [ "$1" != "--" ]; do shift; done; shift
for ref; do
  case "$op" in
    install)
      [ "$ref" = "does.not.Exist" ] && { echo "error: No remote refs found for 'does.not.Exist'" >&2; exit 1; }
      echo "Installing $ref"
      printf '%s\t%s/x86_64/stable\n' "$ref" "$ref" >> "$S/$scope" ;;
    uninstall) grep -v "^$ref	" "$S/$scope" > "$S/$scope.new"; mv "$S/$scope.new" "$S/$scope" ;;
  esac
done
"""

PROFILE = 'rp.profile { name = "main", default = true, use = { "apps" } }\n'


class Flatpak(ReproTestCase):
    def setUp(self):
        super().setUp()
        plugin = os.environ.get("REPRO_FLATPAK_PLUGIN")
        if not plugin:
            self.skipTest("built with -Daddons=disabled")
        shutil.copy(plugin, self.sb.plugin_dir() / "librepro-flatpak.so")
        self.state = self.sb.tmp / "flatpak"
        bin_dir = self.sb.tmp / "bin"
        self.state.mkdir()
        bin_dir.mkdir()
        stub = bin_dir / "flatpak"
        stub.write_text(FLATPAK)
        stub.chmod(0o755)
        self.sb.env["PATH"] = f"{bin_dir}:{self.sb.env['PATH']}"
        self.sb.env["FLATPAK_STUB"] = str(self.state)
        self.sb.write("repro.lua", PROFILE)

    def packages(self, *names):
        quoted = ", ".join(f'"{n}"' for n in names)
        self.sb.write("modules/apps.lua", f"return rp.module {{ packages = {{ {quoted} }} }}")

    def log(self):
        p = self.state / "log"
        return p.read_text() if p.exists() else ""

    def installed(self, scope):
        p = self.state / scope
        return [l.split("\t")[0] for l in p.read_text().splitlines()] if p.exists() else []

    def test_system_installation_in_the_system_pass(self):
        self.packages("flatpak:org.mozilla.firefox", "flatpak:org.gnome.Boxes")
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("+ packages flatpak: install org.mozilla.firefox, org.gnome.Boxes", r.out)
        self.assertIn("Installing org.mozilla.firefox", r.out)  # flatpak's own output is not swallowed
        self.assertIn("  $ flatpak install --system --noninteractive -y -- org.mozilla.firefox org.gnome.Boxes",
                      r.err)
        self.assertEqual(self.installed("system"), ["org.mozilla.firefox", "org.gnome.Boxes"])
        self.assertEqual(self.installed("user"), [])
        r = self.sb.run("sync", "-s", root=True)
        self.assertIn("summary: up to date", r.out)
        self.assertIn("flatpak list --system --columns=application,ref", self.log())

    def test_user_installation_in_the_user_pass(self):
        self.packages("flatpak-user:org.gnome.design.IconLibrary")
        r = self.sb.run("sync", "-s", "--dry-run", root=True)
        self.assertOk(r)
        self.assertIn("· packages flatpak-user: install org.gnome.design.IconLibrary  [user pass]", r.out)
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("flatpak install --user --noninteractive -y -- org.gnome.design.IconLibrary", self.log())
        self.assertEqual(self.installed("user"), ["org.gnome.design.IconLibrary"])
        self.assertEqual(self.installed("system"), [])

    def test_application_ids_and_full_refs_count_as_installed(self):
        (self.state / "system").write_text(
            "org.mozilla.firefox\torg.mozilla.firefox/x86_64/stable\n"
            "org.freedesktop.Platform\torg.freedesktop.Platform/x86_64/24.08\n")
        self.packages("flatpak:org.mozilla.firefox", "flatpak:runtime/org.freedesktop.Platform/x86_64/24.08",
                      "flatpak:org.freedesktop.Platform/x86_64/24.08")
        r = self.sb.run("sync", "-s", root=True)
        self.assertOk(r)
        self.assertIn("summary: up to date", r.out)
        self.assertNotIn("flatpak install", self.log())

    def test_default_backend(self):
        self.sb.write("repro.lua", 'rp.option("packages.backend", "flatpak-user")\n' + PROFILE)
        self.packages("org.mozilla.firefox")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("packages = flatpak-user 1.0.0 (system, default)", r.out)
        self.assertEqual(self.installed("user"), ["org.mozilla.firefox"])

    def test_install_failure_hints_at_the_remote(self):
        self.packages("flatpak-user:does.not.Exist")
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_STEP", exit_code=1)
        self.assertIn("error: No remote refs found for 'does.not.Exist'", r.err)
        self.assertIn("packages backend flatpak-user: install flatpak install --user failed (exit 1)", r.err)
        self.assertIn("hint: flatpak's output is shown above; if no remote has it, add one first: "
                      "flatpak remote-add --user --if-not-exists flathub "
                      "https://dl.flathub.org/repo/flathub.flatpakrepo", r.err)

    def test_upgrade_updates_the_installation_before_installs(self):
        self.packages("flatpak:org.mozilla.firefox")
        r = self.sb.run("sync", "-s", "--upgrade", root=True)
        self.assertOk(r)
        self.assertIn("~ upgrade flatpak: upgrade all installed packages", r.out)
        lines = self.log().splitlines()
        update = lines.index("flatpak update --system --noninteractive -y")
        install = next(i for i, l in enumerate(lines) if l.startswith("flatpak install"))
        self.assertLess(update, install)
        self.assertNotIn("--user", self.log())  # the other installation is left alone

    def test_upgrade_with_modules_updates_only_their_apps(self):
        (self.state / "system").write_text("org.mozilla.firefox\torg.mozilla.firefox/x86_64/stable\n"
                                           "org.gnome.Boxes\torg.gnome.Boxes/x86_64/stable\n")
        self.packages("flatpak:org.mozilla.firefox", "flatpak:org.gimp.GIMP")
        r = self.sb.run("sync", "-s", "--upgrade", "apps", root=True)
        self.assertOk(r)
        self.assertIn("~ upgrade flatpak: upgrade org.mozilla.firefox", r.out)
        self.assertIn("flatpak update --system --noninteractive -y -- org.mozilla.firefox", self.log())
        self.assertIn("flatpak install --system --noninteractive -y -- org.gimp.GIMP", self.log())
        self.assertNotIn("Boxes", self.log())

    def test_missing_addon_names_the_package(self):
        (self.sb.plugin_dir() / "librepro-flatpak.so").unlink()
        self.packages("flatpak-user:org.mozilla.firefox")
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_NO_BACKEND")
        self.assertIn("uses packages backend 'flatpak-user', which is not installed", r.err)
        self.assertIn("repro-flatpak", r.err)
        self.assertNotIn("repro-flatpak-user", r.err)


if __name__ == "__main__":
    main()
