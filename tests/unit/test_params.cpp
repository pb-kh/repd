// Parameter validation and resolution through the evaluator (FR-017):
// defaults, profile overrides, type and one_of checks, unknown names.
#include <doctest.h>

#include "eval/evaluate.hpp"
#include "temp_repo.hpp"

using namespace repd;
using repd::testing::TempRepo;

namespace {

struct Harness {
  TempRepo repo;
  RealFs fs;
  Paths paths;
  LuaHost host;
  Evaluator eval;
  Harness()
      : paths(Paths::make(TargetUser{"a", 1, 1, "/home/a", false}, repo.root)),
        host(paths, fs, Mode::User),
        eval(host, fs, ConfigRepository::at(repo.root)) {
    repo.write("repd.lua", "");
    repo.write("modules/term.lua", R"(
      return rp.module {
        params = {
          size = rp.param { default = 11, type = "number", description = "font size" },
          theme = rp.param { default = "dark", one_of = { "dark", "light" } },
          fonts = rp.param { type = "list", default = { "mono" } },
          extra = rp.param { type = "map", default = {} },
          enabled = true,
        },
        with_params = function(p)
          return { env = { TERM_SIZE = tostring(p.size), TERM_THEME = p.theme, TERM_FONT = p.fonts[1] } }
        end,
      })");
    REQUIRE(eval.load_modules());
  }
};

}  // namespace

TEST_CASE("params: declarations are visible to C++ with inferred types") {
  Harness h;
  auto mods = h.eval.load_modules();
  REQUIRE(mods);
  const auto& p = mods->at("term").params;
  CHECK(p.at("size").type == "number");
  CHECK(p.at("size").description == "font size");
  CHECK(p.at("enabled").type == "boolean");
  CHECK(p.at("fonts").type == "list");
  CHECK(p.at("theme").one_of.size() == 2);
}

TEST_CASE("params: defaults and overrides flow into with_params") {
  Harness h;
  auto d = h.eval.resolve("term", Value{});
  REQUIRE(d);
  CHECK(d->env.at("TERM_SIZE") == "11");
  CHECK(d->env.at("TERM_THEME") == "dark");
  auto o = h.eval.resolve("term", Value{Map{{"size", 14}, {"theme", "light"}, {"fonts", List{"fira"}}}});
  REQUIRE(o);
  CHECK(o->env.at("TERM_SIZE") == "14");
  CHECK(o->env.at("TERM_THEME") == "light");
  CHECK(o->env.at("TERM_FONT") == "fira");
  CHECK(o->params.at("enabled").as_bool());
}

TEST_CASE("params: invalid overrides are E_PARAM naming module, param, value") {
  Harness h;
  auto bad_type = h.eval.resolve("term", Value{Map{{"size", "big"}}});
  REQUIRE_FALSE(bad_type);
  CHECK(bad_type.error().code == "E_PARAM");
  CHECK(bad_type.error().message.find("size") != std::string::npos);
  CHECK(bad_type.error().message.find("term") != std::string::npos);
  auto bad_choice = h.eval.resolve("term", Value{Map{{"theme", "blue"}}});
  REQUIRE_FALSE(bad_choice);
  CHECK(bad_choice.error().hint.find("\"dark\", \"light\"") != std::string::npos);
  auto unknown = h.eval.resolve("term", Value{Map{{"sise", 3}}});
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().hint.find("size") != std::string::npos);
  auto bad_list = h.eval.resolve("term", Value{Map{{"fonts", Map{{"a", "b"}}}}});
  REQUIRE_FALSE(bad_list);
  CHECK(bad_list.error().code == "E_PARAM");
}
