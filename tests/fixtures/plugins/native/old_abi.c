/* A plugin built for an older plugin ABI: repd must refuse it (E_PLUGIN_ABI). */
#include <repd/plugin.h>

int repd_plugin_abi(void) {
  return 0;
}

int repd_plugin_init(int abi, const repd_host* host, const repd_host_api* api, repd_registrar* reg,
                     const repd_registrar_api* reg_api) {
  (void)host; (void)api; (void)reg; (void)reg_api;
  return abi == 0 ? 0 : 1;
}
