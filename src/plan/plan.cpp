#include "plan/plan.hpp"

#include <algorithm>

namespace repro {

std::string_view to_string(StepClass c) {
  switch (c) {
    case StepClass::Upgrade: return "upgrade";
    case StepClass::Packages: return "packages";
    case StepClass::Files: return "files";
    case StepClass::Env: return "env";
    case StepClass::Services: return "services";
    case StepClass::Users: return "users";
    case StepClass::Locale: return "locale";
    case StepClass::Udev: return "udev";
    case StepClass::Action: return "action";
    case StepClass::Post: return "post";
  }
  return "?";
}

int Plan::count(CheckResult r) const {
  // Post-steps (udev reload, locale-gen) are consequences, not declared changes.
  return static_cast<int>(std::ranges::count_if(
      steps, [&](const Step& s) { return s.check == r && s.cls != StepClass::Post; }));
}

std::vector<const Step*> Plan::steps_of(const std::string& module) const {
  std::vector<const Step*> out;
  for (const auto& s : steps) {
    if (s.module == module) out.push_back(&s);
  }
  return out;
}

}  // namespace repro
