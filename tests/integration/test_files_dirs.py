"""Directory sources: `copy` manages every file of a tree (e.g. a neovim
config), `link` points one symlink at the whole directory."""

import os

from conftest import ReproTestCase, main

TREE = {
    "init.lua": "require('config.options')\n",
    "lua/config/options.lua": "vim.o.number = true\n",
    "lua/plugins/lsp.lua": "return {}\n",
    "after/ftplugin/lua.lua": "vim.bo.shiftwidth = 2\n",
}


class DirectorySources(ReproTestCase):
    def setUp(self):
        super().setUp()
        self.sb.write("repro.lua", 'rp.profile { name = "main", default = true, use = { "nvim" } }\n')
        for rel, text in TREE.items():
            self.sb.write(f"modules/nvim/files/nvim/{rel}", text)
        self.declare("copy")

    def declare(self, action):
        self.sb.write("modules/nvim/init.lua",
                      f'return rp.module {{ files = {{ {{ dest = "~/.config/nvim", src = "nvim", action = "{action}" }} }} }}\n')

    def nvim(self, rel=""):
        return self.sb.home_path(".config/nvim") / rel

    def test_copy_tree_then_up_to_date(self):
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("nvim                 4 changes", r.out)
        self.assertIn("+ files   ~/.config/nvim/lua/plugins/lsp.lua  copy from modules/nvim/files/nvim/lua/plugins/lsp.lua", r.out)
        for rel, text in TREE.items():
            self.assertEqual(self.nvim(rel).read_text(), text)
            self.assertFalse(self.nvim(rel).is_symlink())
        self.assertIn("summary: up to date", self.sb.run("sync", "-u").out)

    def test_edits_drift_and_foreign_files(self):
        self.assertOk(self.sb.run("sync", "-u"))
        self.nvim("lazy-lock.json").write_text("{}")  # written by the plugin manager
        self.nvim("lua/config/options.lua").write_text("-- local edit\n")
        self.sb.write("modules/nvim/files/nvim/lua/plugins/ui.lua", "return { 'ui' }\n")
        self.sb.write("modules/nvim/files/nvim/init.lua", "require('config')\n")
        r = self.sb.run("status")
        self.assertRegex(r.out, r"nvim\s+stale")
        r = self.sb.run("sync", "-u")
        self.assertOk(r)
        self.assertIn("~/.config/nvim/lua/config/options.lua  restore drift", r.out)
        self.assertIn("~/.config/nvim/init.lua  update (content differs)", r.out)
        self.assertIn("~/.config/nvim/lua/plugins/ui.lua  copy from", r.out)
        self.assertEqual(self.nvim("lua/config/options.lua").read_text(), TREE["lua/config/options.lua"])
        self.assertEqual(self.nvim("lazy-lock.json").read_text(), "{}")

    def test_existing_unmanaged_config_is_adopted_file_by_file(self):
        self.nvim("lua/config").mkdir(parents=True)
        self.nvim("init.lua").write_text("-- my old config\n")
        self.nvim("lua/config/keys.lua").write_text("-- not in the repo\n")
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_BLOCKED", exit_code=1)
        self.assertIn("~/.config/nvim/init.lua", r.err)
        r = self.sb.run("sync", "-u", "--adopt")
        self.assertOk(r)
        self.assertEqual(self.nvim("init.lua").read_text(), TREE["init.lua"])
        backups = [p for p in self.nvim().iterdir() if p.name.startswith("init.lua.repro-adopted-")]
        self.assertEqual(len(backups), 1)
        self.assertEqual(self.nvim("lua/config/keys.lua").read_text(), "-- not in the repo\n")

    def test_link_whole_directory_and_adopt_existing_one(self):
        self.declare("link")
        self.nvim().mkdir(parents=True)
        self.nvim("init.lua").write_text("-- old\n")
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_BLOCKED", exit_code=1)
        self.assertIn("is a directory", r.err)
        r = self.sb.run("sync", "-u", "--adopt")
        self.assertOk(r)
        self.assertTrue(self.nvim().is_symlink())
        self.assertEqual(os.readlink(self.nvim()), str(self.sb.config / "modules/nvim/files/nvim"))
        self.assertEqual(self.nvim("lua/plugins/lsp.lua").read_text(), "return {}\n")
        old = [p for p in self.nvim().parent.iterdir() if p.name.startswith("nvim.repro-adopted-")]
        self.assertEqual(len(old), 1)
        self.assertEqual((old[0] / "init.lua").read_text(), "-- old\n")
        self.assertIn("summary: up to date", self.sb.run("sync", "-u").out)

    def test_switching_from_link_to_copy_is_blocked_not_written_through(self):
        self.declare("link")
        self.assertOk(self.sb.run("sync", "-u"))
        self.declare("copy")
        r = self.sb.run("sync", "-u")
        self.assertError(r, "E_BLOCKED", exit_code=1)
        self.assertIn("~/.config/nvim is a symlink", r.err)
        self.assertEqual((self.sb.config / "modules/nvim/files/nvim/init.lua").read_text(), TREE["init.lua"])


if __name__ == "__main__":
    main()
