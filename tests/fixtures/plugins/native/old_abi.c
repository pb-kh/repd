/* A plugin built for an older plugin ABI: repro must refuse it (E_PLUGIN_ABI). */
#include <repro/plugin.h>

int repro_plugin_abi(void) { return 0; }

int repro_plugin_init(int abi, const repro_host *host, const repro_host_api *api,
                      repro_registrar *reg, const repro_registrar_api *reg_api) {
  (void)host; (void)api; (void)reg; (void)reg_api;
  return abi == 0 ? 0 : 1;
}
