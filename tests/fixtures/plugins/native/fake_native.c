/* Native test plugin (plugin-abi 1): a packages backend that reports nothing
 * installed and logs installs. Used to exercise dlopen loading and ranking. */
#include <repd/plugin.h>

static const repd_host* g_host;
static const repd_host_api* g_api;

static int probe(void *ud) { (void)ud; return 1; }
static repd_status installed(void* ud, const char* const* names, size_t n, unsigned char* out) {
  (void)ud; (void)names;
  for (size_t i = 0; i < n; ++i) out[i] = 0;
  return REPD_OK;
}
static repd_status install(void* ud, const char* const* names, size_t n) {
  (void)ud;
  for (size_t i = 0; i < n; ++i) g_api->log(g_host, names[i]);
  return REPD_OK;
}

static const repd_packages_vt kVt = {probe, installed, install, 0, 0, 0, 0, 0};

int repd_plugin_abi(void) {
  return REPD_PLUGIN_ABI;
}

int repd_plugin_init(int abi, const repd_host* host, const repd_host_api* api, repd_registrar* reg,
                     const repd_registrar_api* reg_api) {
  if (abi != REPD_PLUGIN_ABI) return 1;
  g_host = host;
  g_api = api;
  return reg_api->register_packages(reg, "native-fake", "9.9.9", REPD_PRIV_SYSTEM, &kVt, 0);
}
