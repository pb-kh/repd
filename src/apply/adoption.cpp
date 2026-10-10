// Adoption guard (research R14, FR-016): existing unmanaged destinations are
// refused unless adoption is requested; adopted files keep a timestamped backup.
#include <format>

#include "apply/executors.hpp"

namespace repd {

std::string adoption_backup_path(const std::string& physical, const std::string& stamp) {
  return std::format("{}.repd-adopted-{}", physical, stamp);
}

Error blocked_error(const Step& s, const Paths& paths) {
  std::string what = s.key;
  if (const auto* d = std::get_if<FileDesired>(&s.desired)) what = paths.display(d->logical);
  return Error{std::string(ec::kBlocked),
               std::format("{} {} (module {})", what, s.blocked_reason, s.module),
               "re-run with --adopt, or set adopt = true on the file entry; the existing file is "
               "backed up as <path>.repd-adopted-<timestamp>"};
}

}  // namespace repd
