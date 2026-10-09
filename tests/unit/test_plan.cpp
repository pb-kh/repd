// Scope selection: closure over requires, interface substitution, cycle
// detection and deterministic topological order; fingerprint stability (RC-003).
#include <doctest.h>

#include "eval/host.hpp"
#include "plan/fingerprint.hpp"
#include "plan/graph.hpp"
#include "plan/resolver.hpp"
#include "platform/fs.hpp"

using namespace repro;

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
