// Scope selection: closure over requires, interface substitution, cycle
// detection and deterministic topological order; fingerprint stability (RC-003).
#include <doctest.h>

#include "eval/host.hpp"
#include "plan/builder.hpp"
#include "plan/fingerprint.hpp"
#include "plan/graph.hpp"
#include "plan/resolver.hpp"
#include "platform/fs.hpp"

using namespace repd;

namespace {

ModuleMeta mod(std::string name, std::vector<std::string> req = {},
               std::optional<std::string> provides = std::nullopt, int priority = 100) {
  ModuleMeta m;
  m.name = std::move(name);
  m.requires_ = std::move(req);
  m.provides = std::move(provides);
  m.priority = priority;
  return m;
}

ModuleMeta iface(std::string name) {
  ModuleMeta m;
  m.name = std::move(name);
  m.kind = ModuleKind::Interface;
  m.description = "iface";
  return m;
}

std::map<std::string, ModuleMeta> world(std::initializer_list<ModuleMeta> ms) {
  std::map<std::string, ModuleMeta> out;
  for (const auto& m : ms) out.emplace(m.name, m);
  return out;
}

Result<Closure> select(const std::map<std::string, ModuleMeta>& w, std::vector<std::string> roots,
                       ResolverInputs in = {}) {
  Resolver r(w, std::move(in));
  return select_scope(w, roots, r);
}

Step pkgs(const std::string& module, const std::string& backend, std::vector<std::string> names,
          Mode priv = Mode::System) {
  Step s;
  s.module = module;
  s.cls = StepClass::Packages;
  s.key = backend;
  s.id = module + "/packages/" + backend;
  s.privilege = priv;
  PackagesDesired d{std::move(names), {}, backend, {}};
  for (const auto& n : d.names) d.owners[n] = module;
  s.desired = std::move(d);
  return s;
}

Step file(const std::string& module, Mode priv) {
  Step s;
  s.module = module;
  s.cls = StepClass::Files;
  s.key = "/etc/" + module;
  s.id = module + "/files/" + s.key;
  s.privilege = priv;
  s.desired = FileDesired{};
  return s;
}

using ModuleSteps = std::vector<std::pair<std::string, std::vector<Step>>>;

// "module: backend[a b] ..." for every packages step, in apply order.
std::vector<std::string> batches(const ModuleSteps& ms) {
  std::vector<std::string> out;
  for (const auto& [m, steps] : ms) {
    for (const auto& s : steps) {
      if (s.cls != StepClass::Packages) continue;
      const auto& d = std::get<PackagesDesired>(s.desired);
      std::string names;
      for (const auto& n : d.names) names += (names.empty() ? "" : " ") + n;
      out.push_back(m + ": " + d.backend + "[" + names + "]");
    }
  }
  return out;
}

}  // namespace

TEST_CASE("graph: dependencies come first, ties broken lexicographically") {
  auto w = world({mod("a", {"c"}), mod("b"), mod("c"), mod("z", {"b"}), mod("unused")});
  auto c = select(w, {"z", "a"});
  REQUIRE(c);
  CHECK(c->order == std::vector<std::string>{"b", "c", "a", "z"});
}

TEST_CASE("graph: closure is transitive") {
  auto w = world({mod("a", {"b"}), mod("b", {"c"}), mod("c", {"d"}), mod("d")});
  auto c = select(w, {"a"});
  REQUIRE(c);
  CHECK(c->order == std::vector<std::string>{"d", "c", "b", "a"});
}

TEST_CASE("graph: interfaces are replaced by their resolved provider") {
  auto w = world({mod("app", {"ide"}), iface("ide"), mod("ide/vim", {"base"}, "ide", 10),
                  mod("ide/emacs", {}, "ide", 20), mod("base")});
  ResolverInputs in;
  in.cli = {{"ide", "ide/vim"}};
  auto c = select(w, {"app"}, in);
  REQUIRE(c);
  CHECK(c->order == std::vector<std::string>{"base", "ide/vim", "app"});
  CHECK(c->providers.at("ide").provider == "ide/vim");
  CHECK(c->providers.at("ide").source == "cli");
}

TEST_CASE("graph: a provider selected directly satisfies its interface (no prompt)") {
  auto w = world({mod("app", {"ide"}), iface("ide"), mod("ide/vim", {}, "ide"),
                  mod("ide/emacs", {}, "ide")});
  ResolverInputs in;
  in.interactive = false;
  auto c = select(w, {"app", "ide/emacs"}, in);
  REQUIRE(c);
  CHECK(c->providers.at("ide").provider == "ide/emacs");
  CHECK(c->order == std::vector<std::string>{"ide/emacs", "app"});
}

TEST_CASE("graph: cycles are reported with the chain") {
  auto w = world({mod("a", {"b"}), mod("b", {"c"}), mod("c", {"a"}), mod("root", {"a"})});
  auto c = select(w, {"root"});
  REQUIRE_FALSE(c);
  CHECK(c.error().code == "E_CYCLE");
  CHECK(c.error().message.find("a → b → c → a") != std::string::npos);
}

TEST_CASE("graph: self loop and two-node cycle") {
  auto w = world({mod("a", {"a"})});
  auto c = select(w, {"a"});
  REQUIRE_FALSE(c);
  CHECK(c.error().code == "E_CYCLE");
  auto w2 = world({mod("x", {"y"}), mod("y", {"x"})});
  auto c2 = select(w2, {"y"});
  REQUIRE_FALSE(c2);
  CHECK(c2.error().message.find("x → y → x") != std::string::npos);
}

TEST_CASE("graph: cycle through an interface") {
  auto w = world({mod("app", {"ide"}), iface("ide"), mod("ide/vim", {"app"}, "ide")});
  auto c = select(w, {"app"});
  REQUIRE_FALSE(c);
  CHECK(c.error().code == "E_CYCLE");
}

TEST_CASE("graph: requested interface as root is resolved") {
  auto w = world({iface("ide"), mod("ide/vim", {}, "ide")});
  auto c = select(w, {"ide"});
  REQUIRE(c);
  CHECK(c->order == std::vector<std::string>{"ide/vim"});
  CHECK(c->providers.at("ide").source == "only-option");
}

TEST_CASE("graph: unknown root") {
  auto w = world({mod("dev/git")});
  auto c = select(w, {"dev/gti"});
  REQUIRE_FALSE(c);
  CHECK(c.error().code == "E_UNKNOWN");
  CHECK(c.error().hint.find("dev/git") != std::string::npos);
}

TEST_CASE("fingerprint: canonical, order-independent, sensitive to content") {
  RealFs fs;
  const Paths paths = Paths::make(TargetUser{"a", 1, 1, "/home/a", false}, "/cfg");
  LuaHost host(paths, fs, Mode::User);
  Map rec1{{"mode", "user"}, {"module", "base"}, {"decl", Map{{"packages", List{"git", "vim"}}}},
           {"providers", Map{}}, {"n", 1}};
  Map rec2;  // same content, different construction order
  rec2.emplace("providers", Map{});
  rec2.emplace("n", 1.0);
  rec2.emplace("decl", Map{{"packages", List{"git", "vim"}}});
  rec2.emplace("module", "base");
  rec2.emplace("mode", "user");
  auto a = fingerprint(host, Value{rec1});
  auto b = fingerprint(host, Value{rec2});
  REQUIRE(a);
  REQUIRE(b);
  CHECK(*a == *b);
  CHECK(a->size() == 64);
  rec2["decl"] = Map{{"packages", List{"vim", "git"}}};
  CHECK(*fingerprint(host, Value{rec2}) != *a);
  rec2 = rec1;
  rec2["mode"] = "system";
  CHECK(*fingerprint(host, Value{rec2}) != *a);
  // Known value pins the encoding format itself (any change is a fingerprint schema bump).
  // sha256("{a=1}")
  CHECK(*fingerprint(host, Value{Map{{"a", 1}}}) ==
        "16f764774c91b93a1e6c8d401cdf1a0108f19fc31ae8733b6fd9b206b0533395");
}

TEST_CASE("batch: independent modules share one install per backend, at the first module") {
  ModuleSteps ms = {
      {"a", {pkgs("a", "pacman", {"git", "vim"}), pkgs("a", "yay", {"paru"}, Mode::User)}},
      {"b", {pkgs("b", "pacman", {"vim", "curl"})}},
      {"c", {pkgs("c", "yay", {"zen"}, Mode::User), file("c", Mode::User)}},
  };
  batch_packages(ms, {});
  CHECK(batches(ms) == std::vector<std::string>{"a: pacman[git vim curl]", "a: yay[paru zen]"});
  const auto& d = std::get<PackagesDesired>(ms[0].second[0].desired);
  CHECK(d.owners.at("vim") == "a");  // declared twice: installed once, owned by the first
  CHECK(d.owners.at("curl") == "b");
  CHECK(ms[1].second.empty());
  REQUIRE(ms[2].second.size() == 1);  // c keeps its file step
  CHECK(ms[2].second[0].cls == StepClass::Files);
}

TEST_CASE(
    "batch: a dependency with other steps in the same pass after the batch starts a new one") {
  // c requires cfg, whose system file must be written before c's packages.
  ModuleSteps ms = {
      {"a", {pkgs("a", "pacman", {"git"})}},
      {"cfg", {file("cfg", Mode::System)}},
      {"c", {pkgs("c", "pacman", {"linux-surface"})}},
      {"d", {pkgs("d", "pacman", {"htop"})}},
  };
  batch_packages(ms, {{"c", {"cfg"}}});
  // d has no such dependency and joins the newest batch.
  CHECK(batches(ms) == std::vector<std::string>{"a: pacman[git]", "c: pacman[linux-surface htop]"});
}

TEST_CASE("batch: transitive dependencies count; the other pass and earlier modules do not") {
  ModuleSteps ms = {
      {"base", {file("base", Mode::System)}},  // before the batch: already applied
      {"a", {pkgs("a", "pacman", {"git"})}},
      {"dots", {file("dots", Mode::User)}},       // user pass: no order against pacman's pass
      {"mid", {pkgs("mid", "pacman", {"zsh"})}},  // only installs through the same backend
      {"top", {pkgs("top", "pacman", {"fish"})}},
  };
  batch_packages(ms, {{"a", {"base"}}, {"mid", {"dots"}}, {"top", {"mid", "base"}}});
  CHECK(batches(ms) == std::vector<std::string>{"a: pacman[git zsh fish]"});

  ModuleSteps ms2 = {
      {"a", {pkgs("a", "pacman", {"git"})}},
      {"cfg", {file("cfg", Mode::System)}},
      {"mid", {pkgs("mid", "yay", {"paru"}, Mode::User)}},
      {"leaf", {pkgs("leaf", "pacman", {"vim"})}},
  };
  batch_packages(ms2, {{"mid", {"cfg"}}, {"leaf", {"mid"}}});
  // leaf -> mid -> cfg: cfg's system file comes after a's batch.
  CHECK(batches(ms2) ==
        std::vector<std::string>{"a: pacman[git]", "mid: yay[paru]", "leaf: pacman[vim]"});
}

TEST_CASE("batch: steps planned elsewhere (upgrades) also order a dependent's installs") {
  ModuleSteps ms = {
      {"a", {pkgs("a", "pacman", {"git"})}},
      {"b", {}},
      {"c", {pkgs("c", "pacman", {"vim"})}},
  };
  Step up;
  up.module = "b";
  up.cls = StepClass::Upgrade;
  up.key = "flatpak";
  up.privilege = Mode::System;
  up.desired = UpgradeDesired{"flatpak", false, {}, {}};
  batch_packages(ms, {{"c", {"b"}}}, {{"b", {up}}});
  CHECK(batches(ms) == std::vector<std::string>{"a: pacman[git]", "c: pacman[vim]"});
}
