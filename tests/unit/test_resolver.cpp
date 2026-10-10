// Provider resolution precedence (FR-006, research R16):
// cli > profile > pin > lock > only-option > prompt > E_AMBIGUOUS.
#include <doctest.h>

#include "plan/resolver.hpp"

using namespace repd;

namespace {

std::map<std::string, ModuleMeta> ide_world(int n_providers = 2) {
  std::map<std::string, ModuleMeta> w;
  ModuleMeta i;
  i.name = "dev/ide";
  i.kind = ModuleKind::Interface;
  i.description = "editor";
  w.emplace(i.name, i);
  const char* names[] = {"dev/ide/vim", "dev/ide/emacs", "dev/ide/helix"};
  const int prio[] = {10, 20, 10};
  for (int k = 0; k < n_providers; ++k) {
    ModuleMeta p;
    p.name = names[k];
    p.provides = "dev/ide";
    p.priority = prio[k];
    p.description = std::string("provider ") + names[k];
    w.emplace(p.name, p);
  }
  ModuleMeta other;
  other.name = "base";
  w.emplace(other.name, other);
  return w;
}

}  // namespace

TEST_CASE("resolver: precedence order") {
  auto w = ide_world();
  ResolverInputs in;
  in.cli = {{"dev/ide", "dev/ide/emacs"}};
  in.profile = {{"dev/ide", "dev/ide/vim"}};
  in.pins = {{"dev/ide", "dev/ide/vim"}};
  in.lock = {{"dev/ide", LockEntry{"dev/ide/vim", "prompt", "t"}}};
  {
    Resolver r(w, in);
    auto p = r.resolve("dev/ide", {"app"});
    REQUIRE(p);
    CHECK(p->provider == "dev/ide/emacs");
    CHECK(p->source == "cli");
  }
  in.cli.clear();
  in.profile = {{"dev/ide", "dev/ide/emacs"}};
  {
    Resolver r(w, in);
    CHECK(r.resolve("dev/ide", {})->source == "profile");
  }
  in.profile.clear();
  in.pins = {{"dev/ide", "dev/ide/emacs"}};
  {
    Resolver r(w, in);
    auto p = r.resolve("dev/ide", {});
    CHECK(p->source == "pin");
    CHECK(p->provider == "dev/ide/emacs");
  }
  in.pins.clear();
  {
    Resolver r(w, in);
    auto p = r.resolve("dev/ide", {});
    CHECK(p->source == "lock");
    CHECK(p->provider == "dev/ide/vim");
  }
}

TEST_CASE("resolver: only option is selected automatically") {
  auto w = ide_world(1);
  Resolver r(w, ResolverInputs{});
  auto p = r.resolve("dev/ide", {});
  REQUIRE(p);
  CHECK(p->provider == "dev/ide/vim");
  CHECK(p->source == "only-option");
  CHECK(p->persist);
}

TEST_CASE("resolver: non-interactive ambiguity lists candidates by priority") {
  auto w = ide_world(3);
  ResolverInputs in;
  in.interactive = false;
  Resolver r(w, in);
  auto p = r.resolve("dev/ide", {"dev/c++"});
  REQUIRE_FALSE(p);
  CHECK(p.error().code == "E_AMBIGUOUS");
  CHECK(p.error().message.find("dev/ide") != std::string::npos);
  CHECK(p.error().message.find("dev/c++") != std::string::npos);
  // (priority, name): helix(10) < vim(10) < emacs(20)
  const auto& m = p.error().message;
  CHECK(m.find("dev/ide/helix") < m.find("dev/ide/vim"));
  CHECK(m.find("dev/ide/vim") < m.find("dev/ide/emacs"));
  CHECK(p.error().hint.find("--provider dev/ide=") != std::string::npos);
}

TEST_CASE("resolver: prompt is asked once with candidates in priority order") {
  auto w = ide_world(2);
  ResolverInputs in;
  in.interactive = true;
  int asked = 0;
  in.prompt = [&](const std::string& iface, const std::vector<const ModuleMeta*>& cands,
                  const std::vector<std::string>&) -> Result<std::string> {
    ++asked;
    CHECK(iface == "dev/ide");
    REQUIRE(cands.size() == 2);
    CHECK(cands[0]->name == "dev/ide/vim");  // default = lowest priority value
    return cands[1]->name;
  };
  Resolver r(w, in);
  auto p = r.resolve("dev/ide", {});
  REQUIRE(p);
  CHECK(p->provider == "dev/ide/emacs");
  CHECK(p->source == "prompt");
  CHECK(r.resolve("dev/ide", {})->provider == "dev/ide/emacs");
  CHECK(asked == 1);
}

TEST_CASE("resolver: reselect ignores the lock entry") {
  auto w = ide_world(2);
  ResolverInputs in;
  in.lock = {{"dev/ide", LockEntry{"dev/ide/vim", "prompt", "t"}}};
  in.reselect = {"dev/ide"};
  in.interactive = false;
  Resolver r(w, in);
  CHECK(r.resolve("dev/ide", {}).error().code == "E_AMBIGUOUS");
}

TEST_CASE("resolver: invalid overrides are E_PROVIDER, stale lock is a warning") {
  auto w = ide_world(2);
  ResolverInputs in;
  in.cli = {{"dev/ide", "base"}};
  {
    Resolver r(w, in);
    auto p = r.resolve("dev/ide", {});
    REQUIRE_FALSE(p);
    CHECK(p.error().code == "E_PROVIDER");
    CHECK(p.error().message.find("base") != std::string::npos);
    CHECK(p.error().hint.find("dev/ide/vim") != std::string::npos);
  }
  in.cli.clear();
  in.lock = {{"dev/ide", LockEntry{"dev/ide/gone", "prompt", "t"}}};
  in.interactive = false;
  {
    Resolver r(w, in);
    auto p = r.resolve("dev/ide", {});
    REQUIRE_FALSE(p);
    CHECK(p.error().code == "E_AMBIGUOUS");
    REQUIRE(r.warnings().size() == 1);
    CHECK(r.warnings()[0].find("dev/ide/gone") != std::string::npos);
  }
}

TEST_CASE("resolver: interface without providers") {
  auto w = ide_world(0);
  Resolver r(w, ResolverInputs{});
  auto p = r.resolve("dev/ide", {"dev/c++"});
  REQUIRE_FALSE(p);
  CHECK(p.error().code == "E_NO_PROVIDER");
  CHECK(p.error().message.find("dev/c++") != std::string::npos);
}
