// Discovery, sandboxed evaluation, declaration validation and typo errors.
#include <doctest.h>

#include "eval/evaluate.hpp"
#include "platform/fs.hpp"
#include "temp_repo.hpp"

using namespace repro;
using repro::testing::TempRepo;

namespace {

struct Harness {
  TempRepo repo;
  RealFs fs;
  Paths paths;
  LuaHost host;
  Evaluator eval;
  Harness()
      : paths(Paths::make(TargetUser{"alice", 1000, 1000, "/home/alice", false}, repo.root)),
        host(paths, fs, Mode::User),
        eval(host, fs, ConfigRepository::at(repo.root)) {
    repo.write("repro.lua", "");
  }
};

}  // namespace

TEST_CASE("eval: missing repro.lua") {
  TempRepo repo;
  RealFs fs;
  const Paths paths = Paths::make(TargetUser{"a", 1, 1, "/home/a", false}, repo.root);
  LuaHost host(paths, fs, Mode::User);
  Evaluator ev(host, fs, ConfigRepository::at(repo.root));
  auto r = ev.load_root();
  REQUIRE_FALSE(r);
  CHECK(r.error().code == "E_CONFIG");
  CHECK(r.error().hint.find("repro init") != std::string::npos);
}

TEST_CASE("eval: discovery names modules by path") {
  Harness h;
  h.repo.write("modules/base.lua", "return rp.module { description = 'base' }");
  h.repo.write("modules/dev/git.lua", "return rp.module { requires = { 'base' } }");
  h.repo.write("modules/dev/ide/init.lua", "return rp.interface { description = 'an editor' }");
  h.repo.write("modules/dev/ide/vim.lua", "return rp.module { provides = 'dev/ide', priority = 10 }");
  h.repo.write("modules/notes.txt", "ignored");
  auto mods = h.eval.load_modules();
  REQUIRE(mods);
  CHECK(mods->size() == 4);
  CHECK(mods->at("dev/ide").is_interface());
  CHECK(mods->at("dev/ide/vim").provides == "dev/ide");
  CHECK(mods->at("dev/ide/vim").priority == 10);
  CHECK(mods->at("dev/git").requires_ == std::vector<std::string>{"base"});
  CHECK(mods->at("dev/git").source == "modules/dev/git.lua");
  CHECK(mods->at("base").source_sha256.size() == 64);
  RootConfig root;
  CHECK(Evaluator::validate(root, *mods));
}

TEST_CASE("eval: same module defined as x.lua and x/init.lua") {
  Harness h;
  h.repo.write("modules/x.lua", "return rp.module {}");
  h.repo.write("modules/x/init.lua", "return rp.module {}");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().code == "E_COLLISION");
}

TEST_CASE("eval: invalid module file name") {
  Harness h;
  h.repo.write("modules/Dev Tools.lua", "return rp.module {}");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().code == "E_NAME");
}

TEST_CASE("eval: typo in a module field names the field, module and fix") {
  Harness h;
  h.repo.write("modules/base.lua", "return rp.module { pakages = { 'git' } }");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().code == "E_CONFIG");
  CHECK(mods.error().message.find("pakages") != std::string::npos);
  CHECK(mods.error().message.find("base") != std::string::npos);
  CHECK(mods.error().hint.find("packages") != std::string::npos);
}

TEST_CASE("eval: undefined global suggests the rp builder") {
  Harness h;
  h.repo.write("modules/base.lua", "return module { }");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().message.find("modules/base.lua:1") != std::string::npos);
  CHECK(mods.error().hint.find("rp.module") != std::string::npos);
}

TEST_CASE("eval: module file must return a declaration") {
  Harness h;
  h.repo.write("modules/base.lua", "local x = 1");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().message.find("must return rp.module") != std::string::npos);
}

TEST_CASE("eval: name field must match the path") {
  Harness h;
  h.repo.write("modules/base.lua", "return rp.module { name = 'core' }");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().code == "E_NAME");
}

TEST_CASE("eval: interfaces cannot carry content") {
  Harness h;
  h.repo.write("modules/ide.lua", "return rp.interface { description = 'x', packages = { 'vim' } }");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().message.find("interfaces cannot declare 'packages'") != std::string::npos);
}

TEST_CASE("eval: unknown requires suggests the closest module") {
  Harness h;
  h.repo.write("modules/dev/git.lua", "return rp.module {}");
  h.repo.write("modules/app.lua", "return rp.module { requires = { 'dev/gti' } }");
  auto mods = h.eval.load_modules();
  REQUIRE(mods);
  RootConfig root;
  auto v = Evaluator::validate(root, *mods);
  REQUIRE_FALSE(v);
  CHECK(v.error().code == "E_UNKNOWN");
  CHECK(v.error().message.find("app") != std::string::npos);
  CHECK(v.error().hint.find("dev/git") != std::string::npos);
}

TEST_CASE("eval: provides must name an interface") {
  Harness h;
  h.repo.write("modules/a.lua", "return rp.module {}");
  h.repo.write("modules/b.lua", "return rp.module { provides = 'a' }");
  auto mods = h.eval.load_modules();
  REQUIRE(mods);
  RootConfig root;
  CHECK(Evaluator::validate(root, *mods).error().code == "E_CONFIG");
}

TEST_CASE("eval: mutating facade during evaluation is E_DECLARATIVE") {
  Harness h;
  h.repo.write("modules/dev/c++.lua",
               "rp.packages.install({ 'clang' })\nreturn rp.module {}");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().code == "E_DECLARATIVE");
  CHECK(mods.error().message.find("dev/c++") != std::string::npos);
  CHECK(mods.error().hint.find("packages = { \"clang\" }") != std::string::npos);
}

TEST_CASE("eval: root config profiles, pins and options") {
  Harness h;
  h.repo.write("repro.lua", R"(
    rp.option("packages.default_backend", "pacman")
    rp.option("services.backend", "auto")
    rp.pin("dev/ide", "dev/ide/vim")
    rp.profile { name = "laptop", default = true, use = { "base", { "dev/git", params = { email = "x@y" } } } }
    rp.profile { name = "server", use = { "base" }, providers = { ["dev/ide"] = "dev/ide/emacs" } }
  )");
  auto root = h.eval.load_root();
  REQUIRE(root);
  CHECK(root->options.at("packages.default_backend") == "pacman");
  CHECK(root->options.at("services.backend") == "auto");
  CHECK(root->pins.at("dev/ide") == "dev/ide/vim");
  REQUIRE(root->profiles.size() == 2);
  CHECK(root->default_profile()->name == "laptop");
  CHECK(root->find_profile("laptop")->use[1].name == "dev/git");
  CHECK(root->find_profile("laptop")->use[1].params["email"].as_string() == "x@y");
  CHECK(root->find_profile("server")->providers.at("dev/ide") == "dev/ide/emacs");
}

TEST_CASE("eval: packages have no automatic backend; entries are name or backend:name") {
  Harness h;
  h.repo.write("repro.lua", "rp.option('packages.default_backend', 'auto')");
  auto root = h.eval.load_root();
  REQUIRE_FALSE(root);
  CHECK(root.error().message.find("packages.default_backend has no \"auto\"") != std::string::npos);
  CHECK(root.error().hint.find("\"yay:neovim\"") != std::string::npos);

  h.repo.write("repro.lua", "rp.option('packages.backend', 'pacman')");
  root = h.eval.load_root();
  REQUIRE_FALSE(root);
  CHECK(root.error().message.find("renamed to packages.default_backend") != std::string::npos);
  CHECK(root.error().hint == "rp.option(\"packages.default_backend\", \"pacman\")");

  h.repo.write("repro.lua", "");
  h.repo.write("modules/ok.lua", "return rp.module { packages = { 'neovim', 'flatpak:org.mozilla.firefox' } }");
  h.repo.write("modules/bad.lua", "return rp.module { packages = { 'git', ':vim' } }");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().message.find("packages[2] ':vim' is not a package or backend:package") != std::string::npos);
  h.repo.write("modules/bad.lua", "return rp.module { packages = { 'yay:' } }");
  mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().message.find("'yay:'") != std::string::npos);
  h.repo.write("modules/bad.lua", "return rp.module { packages = { 'yay:paru-bin' } }");
  CHECK(h.eval.load_modules());
}

TEST_CASE("eval: root config errors") {
  Harness h;
  h.repo.write("repro.lua", "rp.option('packages.backnd', 'auto')");
  auto root = h.eval.load_root();
  REQUIRE_FALSE(root);
  CHECK(root.error().hint.find("packages.default_backend") != std::string::npos);

  h.repo.write("repro.lua",
               "rp.profile { name = 'a', default = true, use = { 'x' } }\n"
               "rp.profile { name = 'b', default = true, use = { 'x' } }");
  root = h.eval.load_root();
  REQUIRE_FALSE(root);
  CHECK(root.error().message.find("both marked default") != std::string::npos);

  h.repo.write("repro.lua", "");
  h.repo.write("modules/base.lua", "rp.profile { name = 'x', use = { 'y' } } return rp.module {}");
  REQUIRE(h.eval.load_root());
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().message.find("can only be used in repro.lua") != std::string::npos);
}

TEST_CASE("eval: resolve applies params, with_params and module-local src paths") {
  Harness h;
  h.repo.write("modules/dev/git/files/gitconfig", "[user]\n");
  h.repo.write("modules/dev/git/files/hooks/pre-commit", "#!/bin/sh\n");
  h.repo.write("modules/dev/git/init.lua", R"(
    return rp.module {
      params = { email = rp.param { type = "string" }, editor = "vim" },
      files = { { dest = "~/.gitconfig", src = "gitconfig" },
                { dest = "~/.git-hooks/pre-commit", src = "hooks/pre-commit", action = "copy" } },
      env = { GIT_EDITOR = "vim" },
      with_params = function(p, sys)
        return { env = { GIT_EMAIL = p.email }, packages = { "git", p.editor } }
      end,
    }
  )");
  auto mods = h.eval.load_modules();
  REQUIRE(mods);
  auto missing = h.eval.resolve("dev/git", Value{});
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == "E_PARAM");
  CHECK(missing.error().message.find("email") != std::string::npos);

  auto r = h.eval.resolve("dev/git", Value{Map{{"email", "a@b"}}});
  REQUIRE(r);
  CHECK(r->env.at("GIT_EMAIL") == "a@b");
  CHECK(r->env.at("GIT_EDITOR") == "vim");
  CHECK(r->packages == std::vector<std::string>{"git", "vim"});
  REQUIRE(r->files.size() == 2);
  CHECK(r->files[0].src_path == "modules/dev/git/files/gitconfig");
  CHECK(r->files[0].action == "link");
  CHECK(r->files[1].src_path == "modules/dev/git/files/hooks/pre-commit");
  CHECK(r->params.at("editor").as_string() == "vim");
  CHECK(r->decl["with_params"].as_string().starts_with("<function modules/dev/git/init.lua:"));
}

TEST_CASE("eval: a module's files/ directory is not scanned for modules") {
  Harness h;
  h.repo.write("modules/nvim/init.lua", "return rp.module { files = { { dest = '~/.config/nvim', src = 'nvim' } } }");
  h.repo.write("modules/nvim/files/nvim/init.lua", "vim.opt.number = true");
  h.repo.write("modules/nvim/files/nvim/lua/plugins.lua", "return {}");
  auto mods = h.eval.load_modules();
  REQUIRE(mods);
  CHECK(mods->size() == 1);
  CHECK(mods->contains("nvim"));
  // Outside a directory module, a dir called files is just a namespace.
  h.repo.write("modules/files/x.lua", "return rp.module {}");
  mods = h.eval.load_modules();
  REQUIRE(mods);
  CHECK(mods->contains("files/x"));
}

TEST_CASE("eval: single-file modules cannot reference sources") {
  Harness h;
  h.repo.write("modules/a.lua", "return rp.module { files = { { dest = '~/x', src = 'x.conf' } } }");
  REQUIRE(h.eval.load_modules());
  auto r = h.eval.resolve("a", Value{});
  REQUIRE_FALSE(r);
  CHECK(r.error().message.find("single-file module") != std::string::npos);
  CHECK(r.error().hint.find("modules/a/init.lua") != std::string::npos);
  CHECK(r.error().hint.find("modules/a/files/") != std::string::npos);
}

TEST_CASE("eval: src escaping the module's files/ is rejected") {
  Harness h;
  h.repo.write("modules/a/init.lua",
               "return rp.module { files = { { dest = '~/x', src = '../init.lua' } } }");
  REQUIRE(h.eval.load_modules());
  auto r = h.eval.resolve("a", Value{});
  REQUIRE_FALSE(r);
  CHECK(r.error().message.find("points outside modules/a/files/") != std::string::npos);
  h.repo.write("modules/a/init.lua",
               "return rp.module { files = { { dest = '~/x', src = '/etc/passwd' } } }");
  REQUIRE(h.eval.load_modules());
  CHECK_FALSE(h.eval.resolve("a", Value{}));
}

TEST_CASE("eval: with_params conflict and invalid fragment") {
  Harness h;
  h.repo.write("modules/a.lua", R"(
    return rp.module {
      env = { EDITOR = "vim" },
      with_params = function() return { env = { EDITOR = "emacs" } } end,
    })");
  h.repo.write("modules/b.lua", R"(
    return rp.module { with_params = function() return { requires = { "a" } } end })");
  REQUIRE(h.eval.load_modules());
  auto a = h.eval.resolve("a", Value{});
  REQUIRE_FALSE(a);
  CHECK(a.error().code == "E_CONFLICT");
  auto b = h.eval.resolve("b", Value{});
  REQUIRE_FALSE(b);
  CHECK(b.error().code == "E_CONFIG");
}

TEST_CASE("eval: file entry validation") {
  Harness h;
  h.repo.write("modules/a.lua",
               "return rp.module { files = { { dest = '.bashrc', content = 'x' } } }");
  auto mods = h.eval.load_modules();
  REQUIRE_FALSE(mods);
  CHECK(mods.error().hint.find("~/.bashrc") != std::string::npos);
  h.repo.write("modules/a.lua",
               "return rp.module { files = { { dest = '~/x', src = 'a', content = 'b' } } }");
  CHECK_FALSE(h.eval.load_modules());
  h.repo.write("modules/a.lua",
               "return rp.module { files = { { dest = '~/x', src = 'a', mode = '0600' } } }");
  CHECK_FALSE(h.eval.load_modules());
}
