// Interactive provider prompt (research R16): shown on the controlling TTY
// only when stdin and stderr are terminals and --yes is absent.
#pragma once

#include <string>
#include <vector>

#include "eval/declaration.hpp"
#include "util/errors.hpp"

namespace repd {

bool can_prompt(bool yes);
Result<std::string> prompt_provider(const std::string& iface,
                                    const std::vector<const ModuleMeta*>& candidates,
                                    const std::vector<std::string>& requirers);

}  // namespace repd
