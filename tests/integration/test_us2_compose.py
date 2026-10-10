"""US2 acceptance 1-3: dependency order, one prompt, recorded choice reused."""

from conftest import RepdTestCase, main


class Compose(RepdTestCase):
    fixture = "providers"

    def test_dependencies_first_and_prompt_once(self):
        code, out, prompts = self.sb.run_tty("sync", "-u", "dev/c++")
        self.assertEqual(code, 0, out)
        self.assertEqual(prompts, 1, out)
        self.assertIn("interface dev/ide needs a provider (required by dev/c++)", out)
        self.assertRegex(out, r"1\) dev/ide/vim\s+Vim setup  \[default\]")
        self.assertRegex(out, r"2\) dev/ide/emacs\s+Emacs setup")
        self.assertIn("dev/ide = dev/ide/vim (prompt)", out)
        # deterministic topological order: dev/base, dev/ide/vim before dev/c++
        self.assertLess(out.index("  dev/base "), out.index("  dev/ide/vim "))
        self.assertLess(out.index("  dev/ide/vim "), out.index("  dev/c++ "))
        lock = (self.sb.config / "repd.lock.lua").read_text()
        self.assertIn('["dev/ide"] = {', lock)
        self.assertIn('provider = "dev/ide/vim"', lock)
        self.assertIn('source = "prompt"', lock)

        code, out, prompts = self.sb.run_tty("sync", "-u", "dev/c++")
        self.assertEqual(code, 0, out)
        self.assertEqual(prompts, 0, out)
        self.assertIn("dev/ide = dev/ide/vim (lock)", out)
        self.assertIn("summary: up to date", out)
        self.assertEqual((self.sb.config / "repd.lock.lua").read_text(), lock, "no timestamp churn")

    def test_prompt_choice_by_number(self):
        code, out, prompts = self.sb.run_tty("sync", "-u", "dev/c++", answers=["2"])
        self.assertEqual(code, 0, out)
        self.assertIn("dev/ide = dev/ide/emacs (prompt)", out)
        env = self.sb.home_path(".local/share/repd/env.d/dev--ide--emacs.sh").read_text()
        self.assertIn("export VISUAL='emacs'", env)

    def test_dry_run_prompt_does_not_write_lock(self):
        code, out, prompts = self.sb.run_tty("sync", "-u", "dev/c++", "--dry-run")
        self.assertEqual(code, 0, out)
        self.assertEqual(prompts, 1)
        self.assertFalse((self.sb.config / "repd.lock.lua").exists())

    def test_provider_selected_directly_needs_no_prompt(self):
        r = self.sb.run("sync", "-u", "dev/c++", "dev/ide/emacs", "--yes")
        self.assertOk(r)
        self.assertIn("dev/ide = dev/ide/emacs (cli)", r.out)

    def test_list_providers_marks_resolution(self):
        r = self.sb.run("list", "providers")
        self.assertOk(r)
        self.assertRegex(r.out, r"dev/ide\s+A code editor")
        self.assertNotIn("*", r.out)  # unresolved until chosen
        self.assertOk(self.sb.run("sync", "-u", "dev/c++", "--provider", "dev/ide=dev/ide/vim"))
        r = self.sb.run("list", "providers")
        self.assertRegex(r.out, r"\*\s+dev/ide/vim\s+priority 10\s+\(lock\)")
        self.assertRegex(r.out, r"\n\s+dev/ide/emacs\s+priority 20")
        r = self.sb.run("list", "interfaces")
        self.assertIn("providers: dev/ide/vim, dev/ide/emacs", r.out)

    def test_show_reports_unresolved_without_prompting(self):
        r = self.sb.run("show", "dev/c++")
        self.assertOk(r)
        self.assertIn("dev/ide = <unresolved: candidates dev/ide/vim, dev/ide/emacs>", r.out)


if __name__ == "__main__":
    main()
