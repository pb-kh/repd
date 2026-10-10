// Plan steps (data-model.md §2 "Step"). Each step has a deterministic id
// `<module>/<class>/<key>`, a privilege, a class-specific desired state and,
// after checking, a check result plus the text the preview renders.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "eval/declaration.hpp"
#include "platform/paths.hpp"

namespace repro {

// Fixed order of classes within a module; Post (synthesised udev reload /
// locale-gen) runs after the last contributing step.
enum class StepClass { Upgrade, Packages, Files, Env, Services, Users, Locale, Udev, Action, Post };
std::string_view to_string(StepClass c);

enum class CheckResult {
  Pending,      // not checked yet
  Satisfied,    // nothing to do
  NeedsChange,  // will be applied in this pass
  Blocked,      // adoption guard: existing unmanaged destination
  SkippedFp,    // action skipped: module fingerprint unchanged
  OtherMode,    // unsatisfied, belongs to the other pass
};

struct FileDesired {
  std::string logical;      // absolute logical destination
  std::string physical;     // after sysroot redirection
  std::string kind;         // link|copy|create|env|udev
  std::string link_target;  // physical source for links
  std::string source;       // repo-relative source for display ("" for content)
  std::string content;      // desired bytes (non-link)
  std::optional<unsigned> mode;
  bool adopt = false;       // entry-level adopt = true
  std::string vars;         // env files: "K=v, K2=v2" for the preview
  std::string tree_root;    // directory copies: the logical dest of the whole tree
};

// A `# BEGIN repro` … `# END repro` block inside a file repro does not own
// (e.g. the line in ~/.profile that loads the sh env files).
struct BlockDesired {
  std::string logical;
  std::string physical;
  std::vector<std::string> lines;
  std::string purpose;  // preview text
};

// repro sync --upgrade: one upgrade per packages backend, before its installs.
// Full, or targeted (--upgrade MODULE…): the named modules' packages.
struct UpgradeDesired {
  std::string backend;
  bool targeted = false;
  std::vector<std::string> names;      // targeted: declared, without the "backend:" prefix
  std::vector<std::string> installed;  // targeted: filled by the check (the rest is installed fresh)
};

// One install per backend for as many modules as ordering allows (batch_packages):
// the step belongs to the first module of its batch, and `owners` remembers
// which module declared each package.
struct PackagesDesired {
  std::vector<std::string> names;    // without the "backend:" prefix
  std::vector<std::string> missing;  // filled by the check
  std::string backend;               // packages backend that installs them
  std::map<std::string, std::string> owners;  // name -> first declaring module (apply order)

  // Modules whose packages are missing, in apply order of first appearance.
  std::vector<std::string> missing_owners() const;
};

struct ServiceDesired {
  ServiceEntry entry;
  std::vector<std::string> ops;  // enable|disable|start|stop, filled by the check
};

struct UserDesired {
  UserEntry entry;
  bool exists = false;
  bool shell_differs = false;
  std::vector<std::string> missing_groups;
};

struct LocaleDesired {
  std::string part;                  // locales|default
  std::vector<std::string> locales;  // this module's locales
  std::vector<std::string> all_locales;  // union over the selected scope
  std::string default_locale;
};

struct ActionDesired {
  std::string name;
};

struct PostDesired {
  std::string what;  // udev-reload|locale-gen
  StepClass after = StepClass::Udev;
};

using Desired = std::variant<FileDesired, UpgradeDesired, PackagesDesired, ServiceDesired, UserDesired,
                             LocaleDesired, ActionDesired, PostDesired, BlockDesired>;

struct Step {
  std::string id;
  std::string module;
  std::string key;
  StepClass cls = StepClass::Files;
  Mode privilege = Mode::User;
  bool imperative = false;
  Desired desired;

  CheckResult check = CheckResult::Pending;
  std::string summary;         // preview text after the class column
  std::string blocked_reason;  // for Blocked
  bool adopting = false;       // takes over an unmanaged destination (with backup)
  bool record_only = false;    // satisfied, but the ownership ledger needs a row
  bool is_update = false;      // NeedsChange on something that exists ('~' rather than '+')

  bool runs_in_this_pass() const { return check == CheckResult::NeedsChange; }
};

}  // namespace repro
