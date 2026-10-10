// Step building, checks and execution against the in-memory Fs:
// link vs copy vs create, env files, adoption guard, drift restore, privileges.
#include <doctest.h>

#include <unistd.h>

#include <filesystem>

#include "apply/checks.hpp"
#include "apply/executors.hpp"
#include "mem_fs.hpp"
#include "plan/builder.hpp"
#include "state/db.hpp"
#include "state/ledger.hpp"

using namespace repd;
using repd::testing::MemFs;

namespace {

struct Fixture {
  MemFs fs;
  Paths paths = Paths::make(TargetUser{"alice", 1000, 1000, "/home/alice", false}, "/cfg", "/sys");
  ConfigRepository repo = ConfigRepository::at("/cfg");
  std::string db_path;
  Db db;
  std::unique_ptr<Ledger> ledger;
  EnvTargets targets{{"environment.d"}, {"environment.d"}, "/home/alice/.profile"};
  std::string hook_owner;

  Fixture() {
    static int n = 0;
    db_path = (std::filesystem::temp_directory_path() /
               ("repd-apply-" + std::to_string(::getpid()) + "-" + std::to_string(n++) + ".db"))
                  .string();
    db = std::move(*Db::open(db_path, Db::Access::ReadWrite));
    ledger = std::make_unique<Ledger>(db);
    fs.put_dir("/home/alice");
    fs.put_file("/cfg/files/gitconfig", "[user]\n");
    fs.put_file("/cfg/repd.lua", "");
  }
  ~Fixture() {
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + "-wal");
    std::filesystem::remove(db_path + "-shm");
  }

  std::vector<Step> build(const ResolvedModule& m) {
    BuildContext b{paths, fs, repo, {{"pacman", Mode::System}, {"yay", Mode::User}}, {}, targets, hook_owner,
                   "pacman"};
    auto steps = build_module_steps(m, b);
    REQUIRE(steps);
    return *steps;
  }
  CheckContext check_ctx(bool adopt = false) {
    CheckContext c{paths, fs};
    c.mode = Mode::User;
    c.ledger = ledger.get();
    c.adopt = adopt;
    return c;
  }
  void check(std::vector<Step>& steps, bool adopt = false) {
    auto c = check_ctx(adopt);
    REQUIRE(check_all(steps, c));
  }
  void apply(Step& s) {
    ApplyContext a{paths, fs};
    a.mode = Mode::User;
    a.stamp = "20261009T120000Z";
    auto r = apply_step(s, a);
    REQUIRE(r);
    if (r->managed) REQUIRE(ledger->upsert_managed_file(*r->managed));
  }
};

ResolvedModule module_with_files() {
  ResolvedModule m;
  m.name = "dev/git";
  FileEntry link;
  link.dest = "~/.gitconfig";
  link.src = "gitconfig";
  link.src_path = "files/gitconfig";
  link.action = "link";
  FileEntry copy = link;
  copy.dest = "~/.config/git/config";
  copy.action = "copy";
  copy.mode = 0600;
  FileEntry create;
  create.dest = "~/.hushlogin";
  create.content = "";
  create.action = "create";
  m.files = {link, copy, create};
  m.env = {{"GIT_EDITOR", "vim"}, {"A", "1"}};
  return m;
}

}  // namespace

TEST_CASE("builder: deterministic ids, class order then key order, privileges") {
  Fixture f;
  auto m = module_with_files();
  FileEntry etc;
  etc.dest = "/etc/gitconfig";
  etc.content = "x";
  etc.action = "create";
  m.files.push_back(etc);
  m.system_env = {{"LANG", "C"}};
  m.packages = {"git", "yay:paru-bin", "pacman:vim", "yay:paru-bin"};
  auto steps = f.build(m);
  std::vector<std::string> ids;
  for (const auto& s : steps) ids.push_back(s.id);
  CHECK(ids == std::vector<std::string>{
                   "dev/git/packages/pacman",
                   "dev/git/packages/yay",
                   "dev/git/files//etc/gitconfig",
                   "dev/git/files/~/.config/git/config",
                   "dev/git/files/~/.gitconfig",
                   "dev/git/files/~/.hushlogin",
                   "dev/git/env//etc/environment.d/90-repd-dev--git.conf",
                   "dev/git/env/~/.config/environment.d/90-repd-dev--git.conf",
               });
  CHECK(steps[0].privilege == Mode::System);  // pacman
  CHECK(std::get<PackagesDesired>(steps[0].desired).names == std::vector<std::string>{"git", "vim"});
  CHECK(steps[1].privilege == Mode::User);  // yay
  CHECK(std::get<PackagesDesired>(steps[1].desired).names == std::vector<std::string>{"paru-bin"});
  CHECK(steps[2].privilege == Mode::System);
  CHECK(steps[3].privilege == Mode::User);
  CHECK(steps[6].privilege == Mode::System);
  CHECK(steps[7].privilege == Mode::User);
  CHECK(std::get<FileDesired>(steps[2].desired).physical == "/sys/etc/gitconfig");

  // Without a default backend an unprefixed package is a config error.
  BuildContext none{f.paths, f.fs, f.repo, {{"yay", Mode::User}}, {}, f.targets, "", ""};
  auto r = build_module_steps(m, none);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == "E_CONFIG");
  CHECK(r.error().message == "module dev/git: package git names no backend, and no default is set");
  m.packages = {"flatpak:org.gimp.GIMP"};
  r = build_module_steps(m, none);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == "E_NO_BACKEND");
}

TEST_CASE("builder: scope=system under home is rejected; missing source is reported") {
  Fixture f;
  ResolvedModule m;
  m.name = "a";
  FileEntry e;
  e.dest = "~/x";
  e.content = "y";
  e.action = "create";
  e.scope = "system";
  m.files = {e};
  BuildContext b{f.paths, f.fs, f.repo, {}, {}, f.targets, {}, {}};
  auto r = build_module_steps(m, b);
  REQUIRE_FALSE(r);
  CHECK(r.error().message.find("scope = \"system\"") != std::string::npos);
  m.files[0].scope.reset();
  m.files[0].src_path = "files/missing";
  m.files[0].action = "copy";
  r = build_module_steps(m, b);
  REQUIRE_FALSE(r);
  CHECK(r.error().message.find("files/missing") != std::string::npos);
}

TEST_CASE("env: one file per module and target, quoted for each consumer") {
  const std::map<std::string, std::string> vars = {{"Z", "1"}, {"A", "it's a \\ test"}};
  CHECK(env_file_content("environment.d", "dev/git", vars) ==
        "# Managed by repd (module dev/git). Do not edit.\nA=it's a \\ test\nZ=1\n");
  CHECK(env_file_content("sh", "dev/git", vars) ==
        "# Managed by repd (module dev/git). Do not edit.\nexport A='it'\\''s a \\ test'\nexport "
        "Z='1'\n");
  CHECK(env_file_content("fish", "dev/git", vars) ==
        "# Managed by repd (module dev/git). Do not edit.\nset -gx A 'it\\'s a \\\\ test'\nset -gx "
        "Z '1'\n");
  const Paths p = Paths::make(TargetUser{"alice", 1000, 1000, "/home/alice", false}, "/cfg");
  CHECK(env_file_path(p, "sh", Mode::User, "dev/git") ==
        "/home/alice/.local/share/repd/env.d/dev--git.sh");
  CHECK(env_file_path(p, "sh", Mode::System, "dev/git") == "/etc/profile.d/repd-dev--git.sh");
  CHECK(env_file_path(p, "fish", Mode::User, "dev/git") ==
        "/home/alice/.config/fish/conf.d/repd-dev--git.fish");
  CHECK(env_file_path(p, "fish", Mode::System, "dev/git") == "/etc/fish/conf.d/repd-dev--git.fish");
  CHECK(env_file_path(p, "environment.d", Mode::User, "dev/git") ==
        "/home/alice/.config/environment.d/90-repd-dev--git.conf");
  CHECK(sh_profile_block(p)[1] ==
        "for f in \"$HOME/.local/share/repd/env.d\"/*.sh; do [ -r \"$f\" ] && . \"$f\"; done; "
        "unset f");
}

TEST_CASE("env targets: auto follows the login shell and the init system") {
  MemFs fs;
  const Paths p = Paths::make(TargetUser{"alice", 1000, 1000, "/home/alice", false}, "/cfg", "/sys");
  fs.put_file("/sys/etc/passwd", "alice:x:1000:1000::/home/alice:/usr/bin/fish\n");
  auto t = resolve_env_targets("auto", p, fs);
  REQUIRE(t);
  CHECK(t->user == std::vector<std::string>{"fish"});
  CHECK(t->system == std::vector<std::string>{"sh", "fish"});
  fs.put_dir("/sys/run/systemd/system");
  fs.put_file("/sys/etc/passwd", "alice:x:1000:1000::/home/alice:/bin/zsh\n");
  t = resolve_env_targets("auto", p, fs);
  CHECK(t->user == std::vector<std::string>{"sh", "environment.d"});
  CHECK(t->system == std::vector<std::string>{"sh", "environment.d"});
  CHECK(t->profile == "/home/alice/.zprofile");
  fs.put_file("/sys/etc/passwd", "alice:x:1000:1000::/home/alice:/bin/bash\n");
  CHECK(resolve_env_targets("auto", p, fs)->profile == "/home/alice/.profile");
  fs.put_file("/home/alice/.bash_profile", "");
  CHECK(resolve_env_targets("auto", p, fs)->profile == "/home/alice/.bash_profile");
  t = resolve_env_targets("fish,environment.d", p, fs);
  CHECK(t->user == std::vector<std::string>{"fish", "environment.d"});
  CHECK(t->system == t->user);
  CHECK(resolve_env_targets("nushell", p, fs).error().code == "E_CONFIG");
}

TEST_CASE("env: sh target adds one managed block to the login profile") {
  Fixture f;
  f.targets = EnvTargets{{"sh"}, {"sh"}, "/home/alice/.profile"};
  f.hook_owner = "dev/git";
  f.fs.put_file("/home/alice/.profile", "export PATH=$HOME/bin:$PATH\n", 0600);
  auto steps = f.build(module_with_files());
  f.check(steps);
  const Step* block = nullptr;
  for (const auto& s : steps) {
    if (std::holds_alternative<BlockDesired>(s.desired)) block = &s;
  }
  REQUIRE(block != nullptr);
  CHECK(block->id == "dev/git/env/~/.profile");
  CHECK(block->summary == "~/.profile  add repd block (load ~/.local/share/repd/env.d/*.sh)");
  for (auto& s : steps) f.apply(s);
  const std::string profile = f.fs.read("/home/alice/.profile").value();
  CHECK(profile.starts_with("export PATH=$HOME/bin:$PATH\n# BEGIN repd\n"));
  CHECK(f.fs.lstat("/home/alice/.profile").mode == 0600);
  CHECK(f.fs.read("/home/alice/.local/share/repd/env.d/dev--git.sh")
            .value()
            .find("export GIT_EDITOR='vim'") != std::string::npos);
  auto again = f.build(module_with_files());
  f.check(again);
  for (auto& s : again) CHECK(s.check == CheckResult::Satisfied);
}

TEST_CASE("env: the same variable in two modules of one scope conflicts") {
  ResolvedModule a, b;
  a.name = "a";
  b.name = "b";
  a.env = {{"EDITOR", "vim"}};
  b.env = {{"EDITOR", "emacs"}};
  auto r = check_conflicts({&a, &b});
  REQUIRE_FALSE(r);
  CHECK(r.error().code == "E_CONFLICT");
  CHECK(r.error().message.find("a and b") != std::string::npos);
  b.env.clear();
  b.system_env = {{"EDITOR", "emacs"}};  // other scope: no conflict
  CHECK(check_conflicts({&a, &b}));
}

TEST_CASE("files: fresh apply, then everything is satisfied") {
  Fixture f;
  auto steps = f.build(module_with_files());
  f.check(steps);
  for (auto& s : steps) {
    CHECK(s.check == CheckResult::NeedsChange);
    CHECK_FALSE(s.is_update);
  }
  CHECK(steps[0].summary == "~/.config/git/config  copy from files/gitconfig");
  CHECK(steps[1].summary == "~/.gitconfig  link -> files/gitconfig");
  CHECK(steps[3].summary == "A=1, GIT_EDITOR=vim  (~/.config/environment.d/90-repd-dev--git.conf)");
  for (auto& s : steps) f.apply(s);
  CHECK(f.fs.lstat("/home/alice/.gitconfig").type == FileType::Symlink);
  CHECK(f.fs.readlink("/home/alice/.gitconfig").value() == "/cfg/files/gitconfig");
  CHECK(f.fs.read("/home/alice/.config/git/config").value() == "[user]\n");
  CHECK(f.fs.lstat("/home/alice/.config/git/config").mode == 0600);
  CHECK(f.fs.read("/home/alice/.config/environment.d/90-repd-dev--git.conf")
            .value()
            .find("GIT_EDITOR=vim") != std::string::npos);
  auto again = f.build(module_with_files());
  f.check(again);
  for (auto& s : again) CHECK(s.check == CheckResult::Satisfied);
}

TEST_CASE("files: drift on a managed file is restored without prompting") {
  Fixture f;
  auto steps = f.build(module_with_files());
  f.check(steps);
  for (auto& s : steps) f.apply(s);
  f.fs.put_file("/home/alice/.config/git/config", "[user]\ndrift\n", 0600);
  auto again = f.build(module_with_files());
  f.check(again);
  CHECK(again[0].check == CheckResult::NeedsChange);
  CHECK(again[0].is_update);
  CHECK(again[0].summary == "~/.config/git/config  restore drift");
  f.apply(again[0]);
  CHECK(f.fs.read("/home/alice/.config/git/config").value() == "[user]\n");
}

TEST_CASE("files: changed declaration is an update, mode-only change too") {
  Fixture f;
  auto steps = f.build(module_with_files());
  f.check(steps);
  for (auto& s : steps) f.apply(s);
  f.fs.put_file("/cfg/files/gitconfig", "[user]\nname = A\n");
  auto again = f.build(module_with_files());
  f.check(again);
  CHECK(again[0].summary == "~/.config/git/config  update (content differs)");
  CHECK(again[1].check == CheckResult::Satisfied);  // link target unchanged
  f.fs.put_file("/cfg/files/gitconfig", "[user]\n");
  f.fs.nodes["/home/alice/.config/git/config"].mode = 0644;
  again = f.build(module_with_files());
  f.check(again);
  CHECK(again[0].summary == "~/.config/git/config  update (mode differs)");
}

TEST_CASE("adoption guard: unmanaged different file is blocked; --adopt backs it up") {
  Fixture f;
  f.fs.put_file("/home/alice/.hushlogin", "mine");
  auto steps = f.build(module_with_files());
  f.check(steps);
  Step& hush = steps[2];
  CHECK(hush.check == CheckResult::Blocked);
  CHECK(hush.summary.find("exists and is not managed by repd — re-run with --adopt") !=
        std::string::npos);
  auto e = blocked_error(hush, f.paths);
  CHECK(e.code == "E_BLOCKED");
  CHECK(e.message.find("~/.hushlogin") != std::string::npos);
  CHECK(e.hint.find("--adopt") != std::string::npos);

  auto adopted = f.build(module_with_files());
  f.check(adopted, /*adopt=*/true);
  CHECK(adopted[2].check == CheckResult::NeedsChange);
  CHECK(adopted[2].adopting);
  ApplyContext a{f.paths, f.fs};
  a.stamp = "20261009T120000Z";
  auto r = apply_step(adopted[2], a);
  REQUIRE(r);
  CHECK(r->adopted);
  CHECK(r->backup == "/home/alice/.hushlogin.repd-adopted-20261009T120000Z");
  CHECK(f.fs.read(r->backup).value() == "mine");
  CHECK(f.fs.read("/home/alice/.hushlogin").value() == "");
}

TEST_CASE("adoption guard: entry-level adopt = true") {
  Fixture f;
  f.fs.put_file("/home/alice/.hushlogin", "mine");
  auto m = module_with_files();
  m.files[2].adopt = true;
  auto steps = f.build(m);
  f.check(steps);
  CHECK(steps[2].adopting);
}

TEST_CASE("adoption guard: identical unmanaged file is recorded, not changed") {
  Fixture f;
  f.fs.put_file("/home/alice/.hushlogin", "");
  auto steps = f.build(module_with_files());
  f.check(steps);
  CHECK(steps[2].check == CheckResult::Satisfied);
  CHECK(steps[2].record_only);
}

TEST_CASE("checks: other-pass steps are only counted") {
  Fixture f;
  ResolvedModule m;
  m.name = "sys";
  FileEntry e;
  e.dest = "/etc/motd";
  e.content = "hi";
  e.action = "create";
  m.files = {e};
  auto steps = f.build(m);
  f.check(steps);
  CHECK(steps[0].check == CheckResult::OtherMode);
  f.fs.put_file("/sys/etc/motd", "hi");
  f.check(steps);
  CHECK(steps[0].check == CheckResult::Satisfied);
  CHECK_FALSE(steps[0].record_only);
}

TEST_CASE("locale: managed block keeps other content and is idempotent") {
  LocaleGen g = parse_locale_gen("# comment\n#en_GB.UTF-8 UTF-8\nfr_FR.UTF-8 UTF-8\n");
  CHECK_FALSE(g.has_block);
  CHECK(g.active.contains("fr_FR.UTF-8 UTF-8"));
  const std::string out = render_locale_gen(g, {"de_DE.UTF-8 UTF-8", "en_US.UTF-8 UTF-8"});
  CHECK(out ==
        "# comment\n#en_GB.UTF-8 UTF-8\nfr_FR.UTF-8 UTF-8\n# BEGIN repd\nde_DE.UTF-8 UTF-8\n"
        "en_US.UTF-8 UTF-8\n# END repd\n");
  LocaleGen g2 = parse_locale_gen(out);
  CHECK(g2.has_block);
  CHECK(g2.block == std::vector<std::string>{"de_DE.UTF-8 UTF-8", "en_US.UTF-8 UTF-8"});
  CHECK(render_locale_gen(g2, g2.block) == out);
  CHECK(locale_conf_lang("LC_TIME=x\nLANG=\"en_US.UTF-8\"\n") == "en_US.UTF-8");
}

TEST_CASE("post-steps follow the last contributing step") {
  Fixture f;
  ResolvedModule a, b;
  a.name = "a";
  b.name = "b";
  a.udev = {UdevEntry{"70-a.rules", std::nullopt, std::nullopt, "A"}};
  b.udev = {UdevEntry{"70-b.rules", std::nullopt, std::nullopt, "B"}};
  FileEntry e;
  e.dest = "/etc/b";
  e.content = "b";
  e.action = "create";
  b.files = {e};
  auto steps = f.build(a);
  auto sb = f.build(b);
  steps.insert(steps.end(), sb.begin(), sb.end());
  add_post_steps(steps);
  REQUIRE(steps.size() == 4);
  CHECK(steps.back().id == "b/post/udev-reload");
}

namespace {

ResolvedModule nvim_module() {
  ResolvedModule m;
  m.name = "nvim";
  FileEntry tree;
  tree.dest = "~/.config/nvim";
  tree.src = "nvim";
  tree.src_path = "modules/nvim/files/nvim";
  tree.action = "copy";
  m.files = {tree};
  return m;
}

void put_nvim_tree(MemFs& fs) {
  fs.put_file("/cfg/modules/nvim/files/nvim/init.lua", "require('opts')\n");
  fs.put_file("/cfg/modules/nvim/files/nvim/lua/opts.lua", "vim.o.number = true\n");
  fs.put_file("/cfg/modules/nvim/files/nvim/lua/plugins/lsp.lua", "return {}\n");
  fs.put_file("/cfg/modules/nvim/files/nvim/bin/fmt", "#!/bin/sh\n", 0755);
}

}  // namespace

TEST_CASE("files: copying a directory manages each file in the tree") {
  Fixture f;
  put_nvim_tree(f.fs);
  auto steps = f.build(nvim_module());
  REQUIRE(steps.size() == 4);
  CHECK(steps[0].id == "nvim/files/~/.config/nvim/bin/fmt");
  CHECK(steps[1].id == "nvim/files/~/.config/nvim/init.lua");
  CHECK(steps[2].id == "nvim/files/~/.config/nvim/lua/opts.lua");
  CHECK(steps[3].id == "nvim/files/~/.config/nvim/lua/plugins/lsp.lua");
  f.check(steps);
  CHECK(steps[3].summary == "~/.config/nvim/lua/plugins/lsp.lua  copy from modules/nvim/files/nvim/lua/plugins/lsp.lua");
  for (auto& s : steps) f.apply(s);
  CHECK(f.fs.read("/home/alice/.config/nvim/lua/plugins/lsp.lua").value() == "return {}\n");
  CHECK(f.fs.lstat("/home/alice/.config/nvim/bin/fmt").mode == 0755);  // executable stays executable
  CHECK(f.fs.lstat("/home/alice/.config/nvim/init.lua").mode == 0644);

  // Files nvim writes itself (lazy-lock.json) are not repd's business.
  f.fs.put_file("/home/alice/.config/nvim/lazy-lock.json", "{}");
  f.fs.put_file("/home/alice/.config/nvim/lua/opts.lua", "drift\n");
  f.fs.put_file("/cfg/modules/nvim/files/nvim/lua/plugins/ui.lua", "return { 'ui' }\n");
  auto again = f.build(nvim_module());
  f.check(again);
  REQUIRE(again.size() == 5);
  CHECK(again[0].check == CheckResult::Satisfied);
  CHECK(again[1].check == CheckResult::Satisfied);
  CHECK(again[2].summary == "~/.config/nvim/lua/opts.lua  restore drift");
  CHECK(again[3].check == CheckResult::Satisfied);
  CHECK(again[4].summary == "~/.config/nvim/lua/plugins/ui.lua  copy from modules/nvim/files/nvim/lua/plugins/ui.lua");
  for (auto& s : again) {
    if (s.check == CheckResult::NeedsChange) f.apply(s);
  }
  CHECK(f.fs.read("/home/alice/.config/nvim/lua/opts.lua").value() == "vim.o.number = true\n");
  CHECK(f.fs.read("/home/alice/.config/nvim/lazy-lock.json").value() == "{}");
}

TEST_CASE("files: an explicit mode applies to every file of a copied directory") {
  Fixture f;
  put_nvim_tree(f.fs);
  auto m = nvim_module();
  m.files[0].mode = 0600;
  auto steps = f.build(m);
  for (auto& s : steps) CHECK(std::get<FileDesired>(s.desired).mode == 0600u);
}

TEST_CASE("files: directory copy refuses a symlinked destination root and odd sources") {
  Fixture f;
  put_nvim_tree(f.fs);
  // Left over from linking the whole directory: writing through it would edit the repo.
  f.fs.put_dir("/home/alice/.config");
  f.fs.put_link("/home/alice/.config/nvim", "/cfg/modules/nvim/files/nvim");
  auto steps = f.build(nvim_module());
  f.check(steps);
  for (auto& s : steps) {
    CHECK(s.check == CheckResult::Blocked);
    CHECK(s.summary.find("~/.config/nvim is a symlink, not a directory — remove it") != std::string::npos);
  }

  Fixture empty;
  empty.fs.put_dir("/cfg/modules/nvim/files/nvim");
  BuildContext b{empty.paths, empty.fs, empty.repo, {}, {}, empty.targets, "", ""};
  auto r = build_module_steps(nvim_module(), b);
  REQUIRE_FALSE(r);
  CHECK(r.error().message == "module nvim: source directory modules/nvim/files/nvim has no files");

  Fixture looped;
  put_nvim_tree(looped.fs);
  looped.fs.put_link("/cfg/modules/nvim/files/nvim/lua/self", "/cfg/modules/nvim/files/nvim");
  BuildContext lb{looped.paths, looped.fs, looped.repo, {}, {}, looped.targets, "", ""};
  r = build_module_steps(nvim_module(), lb);
  REQUIRE_FALSE(r);
  CHECK(r.error().message.find("lua/self is a symlink to a directory") != std::string::npos);
}

TEST_CASE("files: linking over an existing directory needs adoption, which moves it aside") {
  Fixture f;
  put_nvim_tree(f.fs);
  f.fs.put_file("/home/alice/.config/nvim/init.lua", "old\n");
  auto m = nvim_module();
  m.files[0].action = "link";
  auto steps = f.build(m);
  REQUIRE(steps.size() == 1);
  f.check(steps);
  CHECK(steps[0].check == CheckResult::Blocked);
  CHECK(steps[0].summary == "~/.config/nvim  is a directory — re-run with --adopt to move it aside, or set adopt = true");
  steps = f.build(m);
  f.check(steps, /*adopt=*/true);
  CHECK(steps[0].adopting);
  CHECK(steps[0].summary == "~/.config/nvim  adopt existing directory (backup kept), then link -> modules/nvim/files/nvim");
  f.apply(steps[0]);
  CHECK(f.fs.readlink("/home/alice/.config/nvim").value() == "/cfg/modules/nvim/files/nvim");
  CHECK(f.fs.lstat("/home/alice/.config/nvim.repd-adopted-20261009T120000Z").type == FileType::Dir);
}
