// Platform layer: path policy, passwd/group parsing, real-fs atomic writes, process spawning.
#include <doctest.h>

#include <unistd.h>

#include <cstdlib>
#include <filesystem>

#include "mem_fs.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"
#include "platform/proc.hpp"
#include "platform/users.hpp"
#include "util/strings.hpp"

using namespace repro;
using repro::testing::MemFs;

namespace {
TargetUser alice() { return TargetUser{"alice", 1000, 1000, "/home/alice", false}; }
}  // namespace

TEST_CASE("paths: tilde expansion and display") {
  const Paths p = Paths::make(alice(), "/home/alice/.config/repro");
  CHECK(p.expand("~") == "/home/alice");
  CHECK(p.expand("~/.bashrc") == "/home/alice/.bashrc");
  CHECK(p.expand("/etc/x") == "/etc/x");
  CHECK(p.display("/home/alice/.bashrc") == "~/.bashrc");
  CHECK(p.display("/home/alicex/f") == "/home/alicex/f");
  CHECK(p.under_home("/home/alice"));
  CHECK_FALSE(p.under_home("/home/alicex"));
}

TEST_CASE("paths: sysroot redirects only non-home paths") {
  const Paths p = Paths::make(alice(), "/cfg", "/sandbox/root");
  CHECK(p.physical("/etc/locale.gen") == "/sandbox/root/etc/locale.gen");
  CHECK(p.physical("~/.bashrc") == "/home/alice/.bashrc");
  CHECK(p.physical("/sandbox/root/etc/x") == "/sandbox/root/etc/x");
  CHECK(p.state_dir(Mode::System) == "/sandbox/root/var/lib/repro");
  CHECK(p.state_dir(Mode::User) == "/home/alice/.local/state/repro");
  const Paths q = Paths::make(alice(), "/cfg");
  CHECK(q.physical("/etc/locale.gen") == "/etc/locale.gen");
}

TEST_CASE("paths: plugin dirs include the user dir only in user mode") {
  const Paths p = Paths::make(alice(), "/cfg");
  CHECK(p.plugin_dirs(Mode::User).size() == 2);
  CHECK(p.plugin_dirs(Mode::System).size() == 1);
  CHECK(p.plugin_dirs(Mode::System).front().first == REPRO_PLUGIN_DIR);
  CHECK(p.plugin_dirs(Mode::User).back().first == "/home/alice/.local/lib/repro");
  CHECK(p.plugin_dirs(Mode::User).back().second == "user");
}

TEST_CASE("paths: detect honours --config, REPRO_CONFIG and XDG") {
  Env env;
  env.vars = {{"HOME", "/home/alice"}, {"XDG_CONFIG_HOME", "/home/alice/xdg"}};
  auto p = Paths::detect(env);
  REQUIRE(p);
  CHECK(p->config_root() == "/home/alice/xdg/repro");
  CHECK(p->user_env_dir() == "/home/alice/xdg/environment.d");
  env.vars["REPRO_CONFIG"] = "~/dots";
  p = Paths::detect(env);
  REQUIRE(p);
  CHECK(p->config_root() == "/home/alice/dots");
  p = Paths::detect(env, PathsOptions{"/srv/cfg/"});
  REQUIRE(p);
  CHECK(p->config_root() == "/srv/cfg");
}

TEST_CASE("users: passwd and group parsing") {
  MemFs fs;
  fs.put_file("/etc/passwd",
              "root:x:0:0::/root:/bin/bash\n"
              "alice:x:1000:1000:Alice:/home/alice:/bin/zsh\n");
  fs.put_file("/etc/group", "wheel:x:10:alice,bob\naudio:x:11:bob\nvideo:x:12:alice\n");
  auto e = find_passwd_entry(fs, "/etc/passwd", "alice");
  REQUIRE(e);
  CHECK(e->shell == "/bin/zsh");
  CHECK(e->uid == 1000);
  CHECK_FALSE(find_passwd_entry(fs, "/etc/passwd", "carol"));
  CHECK(member_groups(fs, "/etc/group", "alice") == std::vector<std::string>{"video", "wheel"});
  CHECK(group_exists(fs, "/etc/group", "audio"));
  CHECK_FALSE(group_exists(fs, "/etc/group", "docker"));
}

TEST_CASE("memfs: fake behaves like a filesystem") {
  MemFs fs;
  fs.put_dir("/home/alice");
  REQUIRE(fs.write_atomic("/home/alice/a", "hello", 0600));
  CHECK(fs.read("/home/alice/a").value() == "hello");
  CHECK(fs.lstat("/home/alice/a").mode == 0600);
  REQUIRE(fs.symlink_atomic("/home/alice/a", "/home/alice/l"));
  CHECK(fs.lstat("/home/alice/l").type == FileType::Symlink);
  CHECK(fs.stat("/home/alice/l").type == FileType::File);
  CHECK(fs.read("/home/alice/l").value() == "hello");
  CHECK_FALSE(fs.write_atomic("/nope/x", "y", std::nullopt));
  CHECK(fs.list("/home/alice").value() == std::vector<std::string>{"a", "l"});
}

TEST_CASE("realfs: atomic write, symlink, mkdirs, list") {
  namespace stdfs = std::filesystem;
  const stdfs::path dir = stdfs::temp_directory_path() / ("repro-test-" + std::to_string(::getpid()));
  stdfs::remove_all(dir);
  RealFs fs;
  const std::string base = dir.string();
  REQUIRE(fs.mkdirs(base + "/a/b", 0755));
  CHECK(fs.stat(base + "/a/b").type == FileType::Dir);
  REQUIRE(fs.write_atomic(base + "/a/b/f", "data", 0640));
  CHECK(fs.read(base + "/a/b/f").value() == "data");
  CHECK(fs.stat(base + "/a/b/f").mode == 0640);
  REQUIRE(fs.write_atomic(base + "/a/b/f", "new", std::nullopt));
  CHECK(fs.read(base + "/a/b/f").value() == "new");
  REQUIRE(fs.symlink_atomic(base + "/a/b/f", base + "/a/link"));
  CHECK(fs.readlink(base + "/a/link").value() == base + "/a/b/f");
  REQUIRE(fs.symlink_atomic(base + "/elsewhere", base + "/a/link"));
  CHECK(fs.readlink(base + "/a/link").value() == base + "/elsewhere");
  CHECK(fs.list(base + "/a").value() == std::vector<std::string>{"b", "link"});
  CHECK(fs.read(base + "/missing").error().code == "E_IO");
  stdfs::remove_all(dir);
}

TEST_CASE("proc: argv spawn with captured output and exit status") {
  auto r = run_process({"sh", "-c", "echo out; echo err >&2; exit 3"});
  REQUIRE(r);
  CHECK(r->code == 3);
  CHECK(r->out == "out\n");
  CHECK(r->err == "err\n");
  auto s = run_process({"cat"}, ProcOptions{.stdin_data = "piped"});
  REQUIRE(s);
  CHECK(s->out == "piped");
  auto missing = run_process({"repro-definitely-not-a-command"});
  CHECK_FALSE(missing);
  // argv is never interpreted by a shell
  auto lit = run_process({"echo", "$HOME;", "x"});
  REQUIRE(lit);
  CHECK(lit->out == "$HOME; x\n");
}

TEST_CASE("strings: helpers") {
  CHECK(normalize_path("/a/./b//c/../d") == "/a/b/d");
  CHECK(normalize_path("/../x").empty());
  CHECK(closest_match("pakages", std::vector<std::string>{"packages", "files"}) == "packages");
  CHECK(closest_match("zzzzzz", std::vector<std::string>{"packages"}).empty());
  CHECK(closest_match("vim", std::vector<std::string>{"dev/ide/vim", "dev/ide/emacs"}) ==
        "dev/ide/vim");
  CHECK(starts_with_path("/home/a/x", "/home/a"));
  CHECK_FALSE(starts_with_path("/home/ab", "/home/a"));
  CHECK(split("a,,b", ',') == std::vector<std::string>{"a", "b"});
  CHECK(split("a,,b", ',', true) == std::vector<std::string>{"a", "", "b"});
}
