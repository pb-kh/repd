"""US5 acceptance 2: a required backend that is missing is an actionable error."""

import os
import shutil

from conftest import ReproTestCase, main


class MissingBackend(ReproTestCase):
    fixture = "needs-pkgs"

    def use_pacman(self):
        self.sb.write("repro.lua", 'rp.option("packages.default_backend", "pacman")\n'
                      'rp.profile { name = "main", default = true, use = { "dev/c++" } }\n')

    def test_names_the_addon(self):
        self.use_pacman()
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_NO_BACKEND")
        self.assertIn("module dev/c++ uses packages backend 'pacman', which is not installed", r.err)
        self.assertIn("install the repro-pacman add-on", r.err)

    def test_prefixed_package_names_its_addon(self):
        self.sb.add_plugin("fake-packages.lua")
        self.sb.add_plugin("fake-services.lua")
        self.sb.write("modules/dev/c++.lua", 'return rp.module { packages = { "clang", "flatpak:org.gnome.Builder" } }')
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_NO_BACKEND")
        self.assertIn("module dev/c++ uses packages backend 'flatpak', which is not installed", r.err)
        self.assertIn("repro-flatpak", r.err)
        self.assertIn("installed packages backends: fake", r.err)

    def test_unprefixed_package_without_default(self):
        self.sb.add_plugin("fake-packages.lua")
        self.sb.write("repro.lua", 'rp.profile { name = "main", default = true, use = { "dev/c++" } }\n')
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_CONFIG")
        self.assertIn("module dev/c++: package clang names no backend, and no default is set", r.err)
        self.assertIn('write "fake:clang", or set a default in repro.lua: rp.option("packages.default_backend", "fake")', r.err)

    def test_pacman_on_the_system_suggests_pacman_install(self):
        self.use_pacman()
        pacman = self.sb.sys_path("/usr/bin/pacman")
        pacman.parent.mkdir(parents=True)
        pacman.write_text("#!/bin/sh\n")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_NO_BACKEND")
        self.assertIn("pacman -S repro-pacman", r.err)

    def test_services_backend_missing_after_packages_found(self):
        self.sb.add_plugin("fake-packages.lua")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_NO_BACKEND")
        self.assertIn("services backend", r.err)
        self.assertIn("repro-systemd", r.err)

    def test_named_backend_option_not_installed(self):
        self.sb.add_plugin("fake-packages.lua")
        self.sb.add_plugin("fake-services.lua")
        self.use_pacman()
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_NO_BACKEND")
        self.assertIn("'pacman', which is not installed", r.err)
        self.assertIn("installed packages backends: fake", r.err)

    def test_stale_native_plugin_is_refused_fail_soft(self):
        d = self.sb.plugin_dir()
        shutil.copy(os.environ["REPRO_OLD_ABI_PLUGIN"], d / "librepro-old.so")
        self.sb.add_plugin("fake-packages.lua")
        self.sb.add_plugin("fake-services.lua")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertRegex(r.err, r"error\[E_PLUGIN_ABI\]: .*old\.so built for plugin-abi 0, this repro supports 1")
        r = self.sb.run("plugins", "list")
        self.assertIn("refused:", r.out)
        self.assertIn("plugin-abi 0 ≠ 1", r.out)


if __name__ == "__main__":
    main()
