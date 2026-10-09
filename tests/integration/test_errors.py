"""SC-007 error catalogue: every E_* code from contracts/cli.md, each with the
expected exit code, a message naming the offending element, and a hint that
suggests a next action."""

import fcntl
import os
import shutil
import sqlite3

from conftest import ReproTestCase, main

BASE = {"repro.lua": "", "modules/base.lua": "return rp.module {}"}


def case(code, exit_code, args, files=None, element=None, hint=None, setup=None, root=False):
    return dict(code=code, exit=exit_code, args=args, files=files or {}, element=element,
                hint=hint, setup=setup, root=root)


def hold_run_lock(sb):
    d = sb.home / ".local/state/repro"
    d.mkdir(parents=True, exist_ok=True)
    f = open(d / "run.lock", "w")
    f.write("4242\n")
    f.flush()
    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
    return f


def newer_schema(sb):
    d = sb.home / ".local/state/repro"
    d.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(d / "repro.db")
    con.execute("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)")
    con.execute("INSERT INTO meta VALUES ('schema_version', '7')")
    con.commit()
    con.close()


def stale_plugin(sb):
    shutil.copy(os.environ["REPRO_OLD_ABI_PLUGIN"], sb.plugin_dir() / "librepro-old.so")


CATALOGUE = {
    "usage_mode": case("E_USAGE", 2, ["sync"], BASE, r"-u .*-s", r"repro sync -u"),
    "usage_list": case("E_USAGE", 2, ["list", "modles"], BASE, r"modles", r"did you mean 'modules'"),
    "usage_provider_flag": case("E_USAGE", 2, ["sync", "-u", "base", "--provider", "x"], BASE,
                                r"IFACE=MODULE", r"--provider dev/ide="),
    "config_missing_repo": case("E_CONFIG", 2, ["sync", "-u", "--dry-run"], {}, r"no repro\.lua", r"repro init"),
    "config_typo_field": case("E_CONFIG", 2, ["sync", "-u", "base", "--dry-run"],
                              {"repro.lua": "", "modules/base.lua": "return rp.module { pakages = {} }"},
                              r"'pakages'.*module base", r"did you mean 'packages'"),
    "config_syntax": case("E_CONFIG", 2, ["sync", "-u", "base", "--dry-run"],
                          {"repro.lua": "", "modules/base.lua": "return rp.module {"},
                          r"modules/base\.lua", r"fix the Lua syntax"),
    "config_undefined_global": case("E_CONFIG", 2, ["sync", "-u", "base", "--dry-run"],
                                    {"repro.lua": "", "modules/base.lua": "return module {}"},
                                    r"'module' at modules/base\.lua:1", r"rp\.module"),
    "name": case("E_NAME", 2, ["sync", "-u", "--dry-run"],
                 {"repro.lua": "", "modules/Base.lua": "return rp.module {}"}, r"modules/Base\.lua",
                 r"lowercase"),
    "unknown_module": case("E_UNKNOWN", 2, ["sync", "-u", "bsae", "--dry-run"], BASE, r"'bsae'",
                           r"did you mean 'base'"),
    "unknown_requires": case("E_UNKNOWN", 2, ["sync", "-u", "base", "--dry-run"],
                             {"repro.lua": "", "modules/base.lua": "return rp.module { requires = { 'gti' } }",
                              "modules/git.lua": "return rp.module {}"},
                             r"module base requires 'gti'", r"did you mean 'git'"),
    "collision": case("E_COLLISION", 2, ["sync", "-u", "--dry-run"],
                      {"repro.lua": "", "modules/x.lua": "return rp.module {}",
                       "modules/x/init.lua": "return rp.module {}"}, r"module x is defined twice",
                      r"keep either"),
    "cycle": case("E_CYCLE", 2, ["sync", "-u", "a", "--dry-run"],
                  {"repro.lua": "", "modules/a.lua": "return rp.module { requires = { 'b' } }",
                   "modules/b.lua": "return rp.module { requires = { 'a' } }"}, r"a → b → a", r"remove one"),
    "no_provider": case("E_NO_PROVIDER", 2, ["sync", "-u", "app", "--dry-run"],
                        {"repro.lua": "", "modules/ide.lua": "return rp.interface { description = 'x' }",
                         "modules/app.lua": "return rp.module { requires = { 'ide' } }"},
                        r"interface ide \(required by app\)", r'provides = "ide"'),
    "ambiguous": case("E_AMBIGUOUS", 2, ["sync", "-u", "app", "--dry-run", "--yes"],
                      {"repro.lua": "", "modules/ide.lua": "return rp.interface { description = 'x' }",
                       "modules/vim.lua": "return rp.module { provides = 'ide' }",
                       "modules/emacs.lua": "return rp.module { provides = 'ide' }",
                       "modules/app.lua": "return rp.module { requires = { 'ide' } }"},
                      r"interface ide .*emacs, vim", r"--provider ide=emacs"),
    "provider": case("E_PROVIDER", 2, ["sync", "-u", "app", "--dry-run"],
                     {"repro.lua": "rp.pin('ide', 'app')", "modules/ide.lua": "return rp.interface { description = 'x' }",
                      "modules/vim.lua": "return rp.module { provides = 'ide' }",
                      "modules/app.lua": "return rp.module { requires = { 'ide' } }"},
                     r"selects app for interface ide", r"choose one of: vim"),
    "param": case("E_PARAM", 2, ["sync", "-u", "--dry-run"],
                  {"repro.lua": "rp.profile { name='p', default=true, use={ { 'term', params = { size = 'big' } } } }",
                   "modules/term.lua": "return rp.module { params = { size = 12 } }"},
                  r"param 'size' of module term expects a number, got \"big\"", r"pass a number"),
    "conflict": case("E_CONFLICT", 2, ["sync", "-u", "a", "b", "--dry-run"],
                     {"repro.lua": "", "modules/a.lua": "return rp.module { env = { EDITOR = 'vim' } }",
                      "modules/b.lua": "return rp.module { env = { EDITOR = 'nano' } }"},
                     r"EDITOR is set by both a and b", r"only one module"),
    "declarative": case("E_DECLARATIVE", 2, ["sync", "-u", "base", "--dry-run"],
                        {"repro.lua": "", "modules/base.lua": "rp.services.enable('sshd')\nreturn rp.module {}"},
                        r"rp\.services\.enable\(\) .*\(module base\)", r"declare it instead: services ="),
    "no_backend": case("E_NO_BACKEND", 2, ["sync", "-u", "base", "--dry-run"],
                       {"repro.lua": "", "modules/base.lua": "return rp.module { packages = { 'pacman:git' } }"},
                       r"module base uses packages backend 'pacman', which is not installed", r"repro-pacman"),
    "plugin_abi": case("E_PLUGIN_ABI", 0, ["sync", "-u", "base", "--dry-run"], BASE,
                       r"old\.so built for plugin-abi 0, this repro supports 1", r"rebuild or update",
                       setup=stale_plugin),
    "privilege": case("E_PRIVILEGE", 2, ["sync", "-s"], BASE, r"repro sync -s needs root",
                      r"sudo repro sync -s"),
    "busy": case("E_BUSY", 2, ["sync", "-u", "base"], BASE, r"active in user mode \(pid 4242\)",
                 r"wait for it", setup=hold_run_lock),
    "blocked": case("E_BLOCKED", 1, ["sync", "-u", "base"],
                    {"repro.lua": "", "modules/base.lua": "return rp.module { files = { { dest = '~/.vimrc', content = 'x' } } }"},
                    r"~/\.vimrc exists and is not managed by repro \(module base\)", r"--adopt",
                    setup=lambda sb: sb.home_path(".vimrc").write_text("mine")),
    "step": case("E_STEP", 1, ["sync", "-u", "base"],
                 {"repro.lua": "", "modules/base.lua": "return rp.module { actions = { rp.action { name = 'boom', mode = 'user', "
                  "check = function() return false end, run = function() error('kaput') end } } }"},
                 r"base/action/boom: action 'boom' \(run\) failed: .*kaput", r"fix the action in module base"),
    "schema": case("E_SCHEMA", 2, ["sync", "-u", "base"], BASE, r"has schema 7, this repro supports 1",
                   r"upgrade repro", setup=newer_schema),
}


class ErrorCatalogue(ReproTestCase):
    fixture = None

    def test_catalogue_covers_contract(self):
        contract = (os.environ.get("REPRO_SOURCE_ROOT", ".")
                    + "/specs/001-repro-core-mvp/contracts/cli.md")
        if os.path.exists(contract):
            import re
            text = open(contract).read()
            listed = set(re.findall(r"\bE_[A-Z_]+\b", text.split("Catalogue")[1].split("##")[0]))
            covered = {c["code"] for c in CATALOGUE.values()}
            self.assertEqual(listed - covered, set(), "uncovered error codes")


def make_test(name, c):
    def test(self):
        for rel, text in c["files"].items():
            self.sb.write(rel, text)
        held = c["setup"](self.sb) if c["setup"] else None
        try:
            r = self.sb.run(*c["args"], root=c["root"])
        finally:
            if held is not None and hasattr(held, "close"):
                held.close()
        self.assertEqual(r.code, c["exit"], repr(r))
        self.assertRegex(r.err, rf"error\[{c['code']}\]: ", repr(r))
        self.assertRegex(r.err, rf"error\[{c['code']}\]: .*{c['element']}", repr(r))
        self.assertRegex(r.err, rf"\n  hint: .*{c['hint']}", repr(r))
    test.__name__ = f"test_{name}"
    return test


for _name, _case in CATALOGUE.items():
    setattr(ErrorCatalogue, f"test_{_name}", make_test(_name, _case))


if __name__ == "__main__":
    main()
