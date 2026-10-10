// repd — declarative environment configuration (contracts/cli.md).
#include <CLI/CLI.hpp>

#include <cstdio>
#include <print>

#include "cli/commands.hpp"

namespace {

using namespace repd;

int usage_error(const std::string& what) {
  return report(
      Error{std::string(ec::kUsage), what, "see `repd --help` or `repd <command> --help`"});
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"repd — declarative, reproducible environment configuration in Lua", "repd"};
  app.require_subcommand(0, 1);
  GlobalOptions g;
  std::string config;
  app.add_option("--config", config,
                 "Config repository root (default: $REPD_CONFIG or ~/.config/repd)");
  app.add_flag("--no-color", g.no_color, "Disable colored output (also: NO_COLOR)");
  bool version = false;
  app.add_flag("--version", version, "Print version information");

  // init
  auto* init = app.add_subcommand("init", "Scaffold a configuration repository");
  std::string init_dir;
  bool init_force = false;
  init->add_option("--dir", init_dir, "Where to create it (default: the config path)");
  init->add_flag("--force", init_force, "Rewrite the starter files if repd.lua exists");

  // list
  auto* list = app.add_subcommand("list", "List modules, interfaces, profiles, providers or plugins");
  std::string list_what;
  std::string list_profile;
  list->add_option("what", list_what, "modules|interfaces|profiles|providers|plugins")->required();
  list->add_option("--profile", list_profile, "Profile used to resolve providers");

  // show
  auto* show = app.add_subcommand("show", "Show a module's resolved declaration and planned steps");
  std::string show_module;
  std::string show_profile;
  show->add_option("module", show_module, "Module name")->required();
  show->add_option("--profile", show_profile, "Profile whose params/providers apply");
  show->add_option("--option", g.options, "Override rp.option KEY=VALUE")->take_all();

  // sync
  auto* sync = app.add_subcommand("sync", "Plan and apply the configuration (one pass per invocation)");
  SyncArgs sa;
  std::string sync_profile;
  sync->add_flag("-u,--user", sa.user, "User pass (unprivileged steps)");
  sync->add_flag("-s,--system", sa.system, "System pass (privileged steps; run with sudo)");
  sync->add_option("modules", sa.modules, "Modules to sync (default: the profile's modules)");
  sync->add_option("--profile", sync_profile, "Profile to use (default: the profile marked default)");
  sync->add_flag("--dry-run", sa.dry_run, "Show the plan; change nothing");
  sync->add_flag("--yes", sa.yes, "Never prompt; ambiguous providers are an error");
  sync->add_flag("--adopt", sa.adopt, "Take over existing unmanaged files (a backup is kept)");
  sync->add_flag("--upgrade", sa.upgrade,
                 "Fully upgrade each packages backend in use before installing; with MODULE…, upgrade only "
                 "the packages those modules declare");
  sync->add_option("--refresh", sa.refresh, "Re-check actions and post-steps of MODULE")->take_first();
  sync->add_option("--provider", sa.providers, "Select IFACE=MODULE (recorded in repd.lock.lua)")
      ->take_first();
  sync->add_option("--reselect", sa.reselect, "Ignore the recorded provider of IFACE")->take_first();
  sync->add_option("--option", g.options, "Override rp.option KEY=VALUE for this run (e.g. yay.review=skip)")
      ->take_all();

  // status
  auto* status = app.add_subcommand("status", "Show per-module status for one pass");
  bool st_user = false, st_system = false;
  std::string st_profile;
  status->add_flag("-u,--user", st_user, "User pass (default)");
  status->add_flag("-s,--system", st_system, "System pass");
  status->add_option("--profile", st_profile, "Profile to use");
  status->add_option("--option", g.options, "Override rp.option KEY=VALUE")->take_all();

  // journal
  auto* journal = app.add_subcommand("journal", "Show the run journal");
  bool jr_user = false, jr_system = false;
  int jr_last = 5;
  std::int64_t jr_run = 0;
  journal->add_flag("-u,--user", jr_user, "User pass (default)");
  journal->add_flag("-s,--system", jr_system, "System pass");
  journal->add_option("--last", jr_last, "Number of recent runs (default 5)");
  journal->add_option("--run", jr_run, "Show one run by id");

  // plugins
  auto* plugins = app.add_subcommand("plugins", "Backend add-ons");
  plugins->require_subcommand(1);
  auto* plugins_list =
      plugins->add_subcommand("list", "List loaded backends (≡ repd list plugins)");

  try {
    app.parse(argc, argv);
  } catch (const CLI::CallForHelp& e) {
    return app.exit(e);
  } catch (const CLI::CallForAllHelp& e) {
    return app.exit(e);
  } catch (const CLI::ParseError& e) {
    return usage_error(e.what());
  }

  if (version) {
    std::println("{}", version_string());
    return kExitOk;
  }
  if (!config.empty()) g.config = config;
  auto opt = [](const std::string& s) { return s.empty() ? std::nullopt : std::optional<std::string>(s); };
  auto mode_of = [](bool u, bool s) -> std::optional<Mode> {
    if (u && s) return std::nullopt;
    return s ? Mode::System : Mode::User;
  };

  if (init->parsed()) return cmd_init(g, opt(init_dir), init_force);
  if (list->parsed()) return cmd_list(g, list_what, opt(list_profile));
  if (show->parsed()) return cmd_show(g, show_module, opt(show_profile));
  if (sync->parsed()) {
    sa.profile = opt(sync_profile);
    std::vector<std::string> args(argv + 1, argv + argc);
    return cmd_sync(g, sa, args);
  }
  if (status->parsed()) {
    auto m = mode_of(st_user, st_system);
    if (!m) return usage_error("choose one of -u or -s");
    return cmd_status(g, *m, opt(st_profile));
  }
  if (journal->parsed()) {
    auto m = mode_of(jr_user, jr_system);
    if (!m) return usage_error("choose one of -u or -s");
    return cmd_journal(g, *m, jr_last, jr_run > 0 ? std::optional<std::int64_t>(jr_run) : std::nullopt);
  }
  if (plugins_list->parsed()) return cmd_plugins_list(g);
  std::print("{}", app.help());
  return kExitConfig;
}
