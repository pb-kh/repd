/* repro native backend plugin C ABI (contracts/plugin-abi.md, research R17).
 * Pure C: no C++ types cross the boundary; all strings are NUL-terminated UTF-8.
 * Host services and registration are passed in as function tables, so the
 * repro executable exports no symbols and plugins link against nothing. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define REPRO_PLUGIN_ABI 1

/* Entry points stay visible even when a plugin builds with -fvisibility=hidden. */
#if defined(__GNUC__)
#define REPRO_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define REPRO_PLUGIN_EXPORT
#endif

typedef struct repro_host repro_host;           /* opaque, owned by repro */
typedef struct repro_registrar repro_registrar; /* opaque, valid during init only */

typedef enum { REPRO_OK = 0, REPRO_ERR = 1 } repro_status;

/* Host services available to plugins (valid for the whole process). */
typedef struct repro_host_api {
  /* Run argv (no shell). Output tails are captured for the journal. */
  int (*exec)(const repro_host *h, const char *const *argv, char **out, char **err);
  void (*log)(const repro_host *h, const char *msg);
  /* Message for failures; an optional second line ("msg\nhint") is shown as the hint. */
  void (*set_error)(const repro_host *h, const char *msg);
  void (*free_str)(char *s);
  int test_mode; /* 1 in -Dtest_hooks builds */
  /* Run argv (no shell) attached to repro's terminal: stdin, stdout and stderr
     are inherited, so the user sees the output and can answer prompts (e.g. a
     sudo password). Nothing is captured; repro shows the command line first.
     Returns the exit status (127 if it cannot start). */
  int (*exec_tty)(const repro_host *h, const char *const *argv);
  /* Current value of an option this plugin declared (declare_option): the
     `repro sync --option` override, else rp.option() in repro.lua, else the
     declared default. NULL for undeclared keys. Valid for the whole process. */
  const char *(*option)(const repro_host *h, const char *key);
} repro_host_api;

typedef struct repro_packages_vt {
  int (*probe)(void *ud); /* 1 = usable on this system */
  repro_status (*installed)(void *ud, const char *const *names, size_t n,
                            unsigned char *out_flags); /* out_flags[i] = 0/1 */
  repro_status (*install)(void *ud, const char *const *names, size_t n);
  repro_status (*remove)(void *ud, const char *const *names, size_t n);
  repro_status (*refresh)(void *ud);
  /* Optional (NULL = unsupported): refresh and upgrade everything installed
     (`pacman -Syu`). Called for `repro sync --upgrade`, before any install. */
  repro_status (*upgrade)(void *ud);
  /* Optional (NULL = unsupported): upgrade just these installed packages, for
     `repro sync --upgrade MODULE…`. One call per run with every package of the
     named modules, so a backend refreshes its databases at most once. */
  repro_status (*upgrade_packages)(void *ud, const char *const *names, size_t n);
  /* Optional (NULL = always): 1 if upgrade_packages may run now; 0 if it would
     refuse (e.g. an option forbids partial upgrades), with the reason set via
     set_error ("msg\nhint"). Called while planning, after options are known. */
  int (*can_upgrade_packages)(void *ud);
} repro_packages_vt;

typedef struct repro_services_vt {
  int (*probe)(void *ud);
  repro_status (*state)(void *ud, const char *unit, const char *scope, int *enabled,
                        int *active); /* -1 = unit unknown */
  repro_status (*enable)(void *ud, const char *unit, const char *scope);
  repro_status (*disable)(void *ud, const char *unit, const char *scope);
  repro_status (*start)(void *ud, const char *unit, const char *scope);
  repro_status (*stop)(void *ud, const char *unit, const char *scope);
} repro_services_vt;

typedef enum { REPRO_PRIV_USER = 0, REPRO_PRIV_SYSTEM = 1 } repro_privilege;

typedef struct repro_registrar_api {
  int (*register_packages)(repro_registrar *r, const char *name, const char *version,
                           repro_privilege priv, const repro_packages_vt *vt, void *ud);
  int (*register_services)(repro_registrar *r, const char *name, const char *version,
                           const repro_services_vt *vt, void *ud);
  /* Declares an option users set with rp.option(key, v) or --option key=v.
     key is "<backend>.<name>" (e.g. "yay.review"); values is a comma-separated
     list of allowed values (first-class validation by repro); default_value is
     one of them. */
  int (*declare_option)(repro_registrar *r, const char *key, const char *values,
                        const char *default_value);
} repro_registrar_api;

/* Exported by the plugin. Return 0 on success. If abi != REPRO_PLUGIN_ABI the
   plugin MUST return non-zero without registering; repro also refuses it. */
REPRO_PLUGIN_EXPORT int repro_plugin_init(int abi, const repro_host *host, const repro_host_api *api,
                                          repro_registrar *reg, const repro_registrar_api *reg_api);
/* Optional: exported for diagnostics before init. */
REPRO_PLUGIN_EXPORT int repro_plugin_abi(void);

#ifdef __cplusplus
}
#endif
