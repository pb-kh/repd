"""Optional requires, "name+" selection, integrations and auto_sync."""

from conftest import ReproTestCase, main


def module(name, **fields):
    """A module that writes ~/out/<name with / as ->, plus extra Lua fields."""
    slug = name.replace("/", "-")
    extra = "".join(f"  {k} = {v},\n" for k, v in fields.items())
    return (f'return rp.module {{\n{extra}'
            f'  files = {{ {{ dest = "~/out/{slug}", content = "{name}\\n" }} }},\n}}\n')


class Optional(ReproTestCase):
    def setUp(self):
        super().setUp()
        self.profile('"dev/rust"')
        self.sb.write("modules/base.lua", module("base"))
        self.sb.write("modules/dev/rust/init.lua", module(
            "dev/rust", requires='{ "base", { "dev/rust/analyzer", optional = true } }'))
        self.sb.write("modules/dev/rust/analyzer.lua", module("dev/rust/analyzer", requires='{ "dev/rust" }'))
        self.sb.write("modules/shell/fish/init.lua", module("shell/fish"))
        self.sb.write("modules/shell/zsh/init.lua", module("shell/zsh"))
        self.sb.write("modules/shell/fish/rust.lua", module(
            "shell/fish/rust", integrates='{ "shell/fish", "dev/rust" }'))
        self.sb.write("modules/shell/zsh/rust.lua", module(
            "shell/zsh/rust", integrates='{ "shell/zsh", "dev/rust" }', auto_sync="false"))

    def profile(self, use):
        self.sb.write("repro.lua", f'rp.profile {{ name = "main", default = true, use = {{ {use} }} }}\n')

    def synced(self):
        out = self.sb.home_path("out")
        return sorted(p.name for p in out.iterdir()) if out.exists() else []

    # ---- optional requires and "+" ----

    def test_optional_requires_are_listed_not_synced(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertEqual(self.synced(), ["base", "dev-rust"])
        self.assertIn('optional, not synced (name one, or add "+" to the module offering it):', r.out)
        self.assertRegex(r.out, r"dev/rust/analyzer\s+offered by dev/rust")

    def test_naming_an_optional_module_syncs_it_after_what_it_requires(self):
        r = self.sb.run("sync", "-u", "dev/rust/analyzer")
        self.assertOk(r)
        self.assertEqual(self.synced(), ["base", "dev-rust", "dev-rust-analyzer"])
        self.assertLess(r.out.index("  dev/rust "), r.out.index("  dev/rust/analyzer "))
        self.assertNotIn("optional, not synced", r.out)

    def test_plus_adds_optional_requires(self):
        self.assertOk(self.sb.run("sync", "-u", "dev/rust+"))
        self.assertEqual(self.synced(), ["base", "dev-rust", "dev-rust-analyzer"])

    def test_plus_in_a_profile(self):
        self.profile('"dev/rust+"')
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertEqual(self.synced(), ["base", "dev-rust", "dev-rust-analyzer"])
        self.assertIn("use: dev/rust+", self.sb.run("list", "profiles").out)

    def test_unknown_module_with_plus(self):
        r = self.sb.run("sync", "-u", "dev/rsut+")
        self.assertError(r, "E_UNKNOWN")
        self.assertIn("unknown module 'dev/rsut+'", r.err)
        self.assertIn("did you mean 'dev/rust'?", r.err)

    # ---- integrations ----

    def test_integration_joins_when_everything_it_integrates_is_synced(self):
        self.profile('"dev/rust", "shell/fish"')
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("shell-fish-rust", self.synced())
        self.assertRegex(r.out, r"integrations:\n  shell/fish/rust\s+active")
        # after both modules it integrates
        self.assertLess(r.out.index("  shell/fish "), r.out.index("  shell/fish/rust "))
        self.assertLess(r.out.index("  dev/rust "), r.out.index("  shell/fish/rust "))

    def test_inactive_integration_is_listed_with_what_it_needs(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertNotIn("shell-fish-rust", self.synced())
        self.assertRegex(r.out, r"shell/fish/rust\s+inactive, needs shell/fish")

    def test_naming_an_integration_brings_what_it_integrates(self):
        self.assertOk(self.sb.run("sync", "-u", "shell/fish/rust"))
        self.assertEqual(self.synced(), ["base", "dev-rust", "shell-fish", "shell-fish-rust"])

    def test_auto_sync_false_integration_waits_to_be_named(self):
        self.profile('"dev/rust", "shell/zsh"')
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertNotIn("shell-zsh-rust", self.synced())
        self.assertRegex(r.out, r"shell/zsh/rust\s+auto_sync = false: name it to sync")
        self.profile('"dev/rust", "shell/zsh", "shell/zsh/rust"')
        self.assertOk(self.sb.run("sync", "-u"))
        self.assertIn("shell-zsh-rust", self.synced())

    def test_profile_turns_auto_sync_off_and_on(self):
        self.profile('"dev/rust", "shell/fish", { "shell/fish/rust", auto_sync = false }')
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertNotIn("shell-fish-rust", self.synced())
        self.assertRegex(r.out, r"shell/fish/rust\s+auto_sync = false")
        self.assertIn("shell/fish/rust (auto_sync = false)", self.sb.run("list", "profiles").out)
        self.profile('"dev/rust", "shell/zsh", { "shell/zsh/rust", auto_sync = true }')
        self.assertOk(self.sb.run("sync", "-u"))
        self.assertIn("shell-zsh-rust", self.synced())

    def test_required_module_with_auto_sync_false_is_synced_anyway(self):
        self.sb.write("modules/base.lua", module("base", auto_sync="false"))
        self.assertOk(self.sb.run("sync", "-u"))
        self.assertIn("base", self.synced())

    def test_integration_completing_another(self):
        # shell/fish/rust/clippy integrates the integration itself
        self.sb.write("modules/shell/fish/rust/clippy.lua", module(
            "shell/fish/rust/clippy", integrates='{ "shell/fish/rust" }'))
        self.profile('"dev/rust", "shell/fish"')
        self.assertOk(self.sb.run("sync", "-u"))
        self.assertIn("shell-fish-rust-clippy", self.synced())

    # ---- listing and validation ----

    def test_list_modules_shows_the_relations(self):
        r = self.sb.run("list", "modules")
        self.assertOk(r)
        self.assertRegex(r.out, r"dev/rust/analyzer\s+optional for dev/rust")
        self.assertRegex(r.out, r"shell/fish/rust\s+integrates shell/fish \+ dev/rust")
        self.assertRegex(r.out, r"shell/zsh/rust\s+integrates shell/zsh \+ dev/rust; auto_sync = false")

    def test_declaration_errors(self):
        self.sb.write("modules/dev/rust/init.lua", module(
            "dev/rust", requires='{ { "dev/rust/analyzr", optional = true } }'))
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_UNKNOWN")
        self.assertIn("module dev/rust requires 'dev/rust/analyzr', which does not exist", r.err)
        self.sb.write("modules/dev/rust/init.lua", module(
            "dev/rust", requires='{ { "dev/rust/analyzer", optoinal = true } }'))
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_CONFIG")
        self.assertIn("unknown field 'optoinal' in requires[1]", r.err)
        self.sb.write("modules/dev/rust/init.lua", module("dev/rust", integrates='{ "shell/fsh" }'))
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_UNKNOWN")
        self.assertIn("module dev/rust integrates 'shell/fsh', which does not exist", r.err)
        self.sb.write("modules/dev/rust/init.lua", module("dev/rust", auto_sync='"no"'))
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_CONFIG")
        self.assertIn("auto_sync must be a boolean", r.err)


if __name__ == "__main__":
    main()
