/* Native test plugin (plugin-abi 1): a packages backend that reports nothing
 * installed and logs installs. Used to exercise dlopen loading and ranking. */
#include <repro/plugin.h>

static const repro_host *g_host;
static const repro_host_api *g_api;

static int probe(void *ud) { (void)ud; return 1; }
static repro_status installed(void *ud, const char *const *names, size_t n, unsigned char *out) {
  (void)ud; (void)names;
  for (size_t i = 0; i < n; ++i) out[i] = 0;
  return REPRO_OK;
}
static repro_status install(void *ud, const char *const *names, size_t n) {
  (void)ud;
  for (size_t i = 0; i < n; ++i) g_api->log(g_host, names[i]);
  return REPRO_OK;
}

static const repro_packages_vt kVt = {probe, installed, install, 0, 0, 0, 0, 0};

int repro_plugin_abi(void) { return REPRO_PLUGIN_ABI; }

int repro_plugin_init(int abi, const repro_host *host, const repro_host_api *api,
                      repro_registrar *reg, const repro_registrar_api *reg_api) {
  if (abi != REPRO_PLUGIN_ABI) return 1;
  g_host = host;
  g_api = api;
  return reg_api->register_packages(reg, "native-fake", "9.9.9", REPRO_PRIV_SYSTEM, &kVt, 0);
}
