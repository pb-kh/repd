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

std::vector<const Step*> Plan::steps_for(const std::string& module) const {
  std::vector<const Step*> out;
  for (const auto& s : steps) {
    if (s.cls == StepClass::Packages) {
      // A batch concerns the modules whose packages it still has to install.
      const auto owners = std::get<PackagesDesired>(s.desired).missing_owners();
      if (std::ranges::find(owners, module) != owners.end()) out.push_back(&s);
    } else if (s.module == module) {
      out.push_back(&s);
    }
  }
  return out;
}

std::vector<std::string> PackagesDesired::missing_owners() const {
  std::vector<std::string> out;
  for (const auto& n : missing) {
    auto it = owners.find(n);
    if (it == owners.end()) continue;
    if (std::ranges::find(out, it->second) == out.end()) out.push_back(it->second);
  }
  return out;
}

}  // namespace repro
