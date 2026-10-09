"""US5 acceptance 4, 6: repo-local Lua backends are flagged reduced-trust;
mutating calls while configuration is read are E_DECLARATIVE."""

from conftest import ReproTestCase, main


class RepoLocal(ReproTestCase):
    fixture = "basic"

    def test_flagged_reduced_trust(self):
        r = self.sb.run("plugins", "list")
        self.assertOk(r)
        self.assertIn("repo ⚠", r.out)
        self.assertIn("(reduced trust)", r.out)
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertIn("reduced trust", r.out)

    def test_backend_scratch_store_persists_between_runs(self):
        self.assertOk(self.sb.run("sync", "-u"))
        mem = self.sb.home / ".local/state/repro/lua-backend-mem/fake.lua"
        self.assertIn('ripgrep=true', mem.read_text())
        self.assertIn("summary: up to date", self.sb.run("sync", "-u").out)

    def test_declarative_violation_in_module(self):
        self.sb.write("modules/base/init.lua", 'rp.packages.install({ "clang" })\nreturn rp.module {}')
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_DECLARATIVE")
        self.assertIn("rp.packages.install() changes the system while configuration is being read (module base)", r.err)
        self.assertIn('declare it instead: packages = { "clang" }', r.err)

    def test_ctx_exec_in_check_is_rejected(self):
        self.sb.write("modules/base/init.lua", """
return rp.module {
  actions = { rp.action { name = "x", mode = "user",
    check = function(ctx) ctx.exec("true") return true end,
    run = function(ctx) end } },
}""")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_DECLARATIVE")
        self.assertIn("ctx.exec() called from the check of action 'x' (module base)", r.err)

    def test_backend_plugin_must_return_backend(self):
        self.sb.write("plugins/lua/broken.lua", "return { }")
        r = self.sb.run("sync", "-u", "--dry-run")
        self.assertError(r, "E_CONFIG")
        self.assertIn("plugins/lua/broken.lua must return rp.backend", r.err)


if __name__ == "__main__":
    main()
