"""US5 acceptance 3, 5: auto-selected backends run package/service steps; listing."""

import os
import shutil
import sqlite3

from conftest import ReproTestCase, main


class Backends(ReproTestCase):
    fixture = "needs-pkgs"

    def setUp(self):
        super().setUp()
        self.sb.add_plugin("fake-packages.lua")
        self.sb.add_plugin("fake-services.lua")

    def test_backends_stated_in_preview(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertRegex(r.out, r"packages = fake lua:[0-9a-f]{8} \(repo ⚠ reduced trust, default\)")
        self.assertRegex(r.out, r"services = fake lua:[0-9a-f]{8} \(repo ⚠ reduced trust, auto\)")
        self.assertIn("+ packages fake: install clang, cmake", r.out)
        self.assertIn("~ services distccd.service (user)  enable, start", r.out)
        self.assertIn("ok    dev/c++/packages/fake", r.out)
        self.assertIn("ok    dev/c++/services/user:distccd.service", r.out)
        r = self.sb.run("sync", "-u")
        self.assertIn("summary: up to date", r.out)

    def test_package_failure_stops_the_run(self):
        self.sb.write("modules/dev/c++.lua", 'return rp.module { packages = { "clang", "fail-me" } }')
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_STEP", exit_code=1)
        self.assertIn("cannot install fail-me", r.err)
        self.assertTrue(r.out.rstrip().splitlines()[-1].startswith("failed: dev/c++/packages/fake:"))
        self.assertRegex(self.sb.run("status").out, r"dev/c\+\+\s+failed")

    def test_plugins_list(self):
        r = self.sb.run("plugins", "list")
        self.assertOk(r)
        self.assertRegex(r.out, r"packages\s+fake\s+lua:[0-9a-f]{8}\s+-\s+repo ⚠\s+plugins/lua/fake-packages\.lua \(reduced trust\)")
        self.assertRegex(r.out, r"services\s+fake\s+lua:[0-9a-f]{8}\s+-\s+repo ⚠")
        self.assertEqual(r.out, self.sb.run("list", "plugins").out)

    def test_two_packages_backends_in_one_module(self):
        d = self.sb.plugin_dir()
        shutil.copy(os.environ["REPRO_NATIVE_PLUGIN"], d / "librepro-native.so")
        r = self.sb.run("plugins", "list")
        self.assertOk(r)
        lines = r.out.splitlines()
        self.assertRegex(lines[1], r"packages\s+native-fake\s+9\.9\.9\s+1\s+system")
        self.sb.write("modules/dev/c++.lua", 'return rp.module { packages = { "clang", "native-fake:htop" } }')
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertRegex(r.out, r"packages = fake lua:[0-9a-f]{8} \(repo ⚠ reduced trust, default\)")
        self.assertIn("packages = native-fake 9.9.9 (system, prefix)", r.out)
        self.assertIn("+ packages fake: install clang", r.out)
        # Each backend's packages go to its own pass: native-fake has system privilege.
        self.assertIn("· packages native-fake: install htop  [system pass]", r.out)
        self.assertIn("ok    dev/c++/packages/fake", r.out)

    def test_upgrade_runs_before_installs_and_only_when_asked(self):
        r = self.sb.run("sync", "-u", "--upgrade")
        self.assertOk(r)
        self.assertIn("~ upgrade fake: upgrade all installed packages", r.out)
        out = r.out
        self.assertLess(out.index("ok    dev/c++/upgrade/fake"), out.index("ok    dev/c++/packages/fake"))
        r = self.sb.run("sync", "-u")
        self.assertIn("summary: up to date", r.out)
        self.assertNotIn("upgrade", r.out)
        r = self.sb.run("journal", "--last", "1")
        self.assertNotIn("upgrade/fake", r.out)

    def test_upgrade_skips_backends_without_the_operation(self):
        d = self.sb.plugin_dir()
        shutil.copy(os.environ["REPRO_NATIVE_PLUGIN"], d / "librepro-native.so")
        self.sb.write("modules/dev/c++.lua", 'return rp.module { packages = { "clang", "native-fake:htop" } }')
        r = self.sb.run("sync", "-u", "--upgrade", "--dry-run")
        self.assertOk(r)
        self.assertIn("warning: --upgrade: packages backend native-fake cannot upgrade; skipped", r.err)
        self.assertIn("~ upgrade fake: upgrade all installed packages", r.out)

    def test_upgrade_includes_an_unused_default_backend(self):
        self.sb.write("modules/dev/c++.lua", 'return rp.module { files = { { dest = "~/x", content = "x" } } }')
        r = self.sb.run("sync", "-u", "--upgrade", "--dry-run")
        self.assertOk(r)
        self.assertIn("~ upgrade fake: upgrade all installed packages", r.out)

    def test_upgrade_with_modules_upgrades_only_their_installed_packages(self):
        self.sb.write("modules/dev/go.lua", 'return rp.module { packages = { "go" } }')
        self.sb.write("repro.lua", 'rp.option("packages.backend", "fake")\n'
                      'rp.profile { name = "main", default = true, use = { "dev/c++", "dev/go" } }\n')
        self.sb.write("modules/dev/c++.lua", 'return rp.module { packages = { "clang" } }')
        self.assertOk(self.sb.run("sync", "-u"))
        self.sb.write("modules/dev/c++.lua", 'return rp.module { packages = { "clang", "cmake" } }')
        r = self.sb.run("sync", "-u", "--upgrade", "dev/c++")
        self.assertOk(r)
        # cmake is not installed yet: installed fresh, not upgraded; dev/go is not named
        self.assertIn("~ upgrade fake: upgrade clang\n", r.out)
        self.assertIn("+ packages fake: install cmake", r.out)
        self.assertNotIn("all installed", r.out)
        self.assertLess(r.out.index("ok    dev/c++/upgrade/fake"), r.out.index("ok    dev/c++/packages/fake"))
        con = sqlite3.connect(self.sb.home / ".local/state/repro/repro.db")
        detail = con.execute("SELECT detail FROM steps WHERE step_id = 'dev/c++/upgrade/fake'").fetchone()[0]
        con.close()
        self.assertIn('upgraded={"clang"}', detail.replace(" ", ""))
        self.assertIn("upgraded clang", detail)
        self.assertNotIn("upgraded everything", detail)
        r = self.sb.run("sync", "-u", "--upgrade", "dev/go", "--dry-run")
        self.assertIn("~ upgrade fake: upgrade go", r.out)

    def test_upgrade_with_modules_and_nothing_installed_yet(self):
        r = self.sb.run("sync", "-u", "--upgrade", "dev/c++", "--dry-run")
        self.assertOk(r)
        self.assertNotIn("upgrade fake", r.out)
        self.assertIn("+ packages fake: install clang, cmake", r.out)

    def test_upgrade_with_modules_skips_backends_without_targeted_upgrades(self):
        d = self.sb.plugin_dir()
        shutil.copy(os.environ["REPRO_NATIVE_PLUGIN"], d / "librepro-native.so")
        self.sb.write("modules/dev/c++.lua", 'return rp.module { packages = { "clang", "native-fake:htop" } }')
        r = self.sb.run("sync", "-u", "--upgrade", "dev/c++", "--dry-run")
        self.assertOk(r)
        self.assertIn("warning: --upgrade: packages backend native-fake cannot upgrade single packages; "
                      "skipped htop", r.err)

    def test_only_librepro_files_are_loaded(self):
        d = self.sb.plugin_dir()
        shutil.copy(os.environ["REPRO_NATIVE_PLUGIN"], d / "native.so")
        shutil.copy(os.environ["REPRO_NATIVE_PLUGIN"], d / "libother-native.so")
        r = self.sb.run("plugins", "list")
        self.assertOk(r)
        self.assertNotIn("native-fake", r.out)
        user = self.sb.home / ".local/lib/repro"
        user.mkdir(parents=True)
        shutil.copy(os.environ["REPRO_NATIVE_PLUGIN"], user / "librepro-native.so")
        r = self.sb.run("plugins", "list")
        self.assertRegex(r.out, r"packages\s+native-fake\s+9\.9\.9\s+1\s+user\s+~/\.local/lib/repro/librepro-native\.so")

    def test_backend_option_selects_by_name(self):
        d = self.sb.plugin_dir()
        shutil.copy(os.environ["REPRO_NATIVE_PLUGIN"], d / "librepro-native.so")
        self.sb.write("repro.lua", 'rp.option("packages.backend", "native-fake")\n'
                      'rp.profile { name = "main", default = true, use = { "dev/c++" } }\n')
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertIn("packages = native-fake 9.9.9 (system, default)", r.out)
        self.assertNotIn("packages = fake", r.out)

    def test_eval_time_query_uses_backend(self):
        self.sb.write("modules/dev/c++.lua", """
local have = rp.packages.installed({ "clang" })
return rp.module { env = { HAVE_CLANG = have.clang and "yes" or "no" } }""")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertOk(r)
        self.assertIn("HAVE_CLANG=no", r.out)


if __name__ == "__main__":
    main()
