// SIGINT/SIGTERM handling (research R15): the signal sets a flag checked
// between steps and is forwarded to the running child; the run is closed with
// exit 130.
#pragma once

namespace repd {

void install_signal_handlers();
bool interrupted();

}  // namespace repd
