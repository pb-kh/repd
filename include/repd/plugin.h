/* repd native backend plugin C ABI (contracts/plugin-abi.md, research R17).
 * Pure C: no C++ types cross the boundary; all strings are NUL-terminated UTF-8.
 * Host services and registration are passed in as function tables, so the
 * repd executable exports no symbols and plugins link against nothing. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define REPD_PLUGIN_ABI 1

/* Entry points stay visible even when a plugin builds with -fvisibility=hidden. */
#if defined(__GNUC__)
#define REPD_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define REPD_PLUGIN_EXPORT
#endif

typedef struct repd_host repd_host;           /* opaque, owned by repd */
typedef struct repd_registrar repd_registrar; /* opaque, valid during init only */

typedef enum { REPD_OK = 0, REPD_ERR = 1 } repd_status;

/* Host services available to plugins (valid for the whole process). */
typedef struct repd_host_api {
  /* Run argv (no shell). Output tails are captured for the journal. */
  int (*exec)(const repd_host* h, const char* const* argv, char** out, char** err);
  void (*log)(const repd_host* h, const char* msg);
  /* Message for failures; an optional second line ("msg\nhint") is shown as the hint. */
  void (*set_error)(const repd_host* h, const char* msg);
  void (*free_str)(char* s);
  int test_mode; /* 1 in -Dtest_hooks builds */
  /* Run argv (no shell) attached to repd's terminal: stdin, stdout and stderr
     are inherited, so the user sees the output and can answer prompts (e.g. a
     sudo password). Nothing is captured; repd shows the command line first.
     Returns the exit status (127 if it cannot start). */
  int (*exec_tty)(const repd_host* h, const char* const* argv);
  /* Current value of an option this plugin declared (declare_option): the
     `repd sync --option` override, else rp.option() in repd.lua, else the
     declared default. NULL for undeclared keys. Valid for the whole process. */
  const char* (*option)(const repd_host* h, const char* key);
} repd_host_api;

typedef struct repd_packages_vt {
  int (*probe)(void* ud); /* 1 = usable on this system */
  repd_status (*installed)(void* ud, const char* const* names, size_t n,
                           unsigned char* out_flags); /* out_flags[i] = 0/1 */
  repd_status (*install)(void* ud, const char* const* names, size_t n);
  repd_status (*remove)(void* ud, const char* const* names, size_t n);
  repd_status (*refresh)(void* ud);
  /* Optional (NULL = unsupported): refresh and upgrade everything installed
     (`pacman -Syu`). Called for `repd sync --upgrade`, before any install. */
  repd_status (*upgrade)(void* ud);
  /* Optional (NULL = unsupported): upgrade just these installed packages, for
     `repd sync --upgrade MODULE…`. One call per run with every package of the
     named modules, so a backend refreshes its databases at most once. */
  repd_status (*upgrade_packages)(void* ud, const char* const* names, size_t n);
  /* Optional (NULL = always): 1 if upgrade_packages may run now; 0 if it would
     refuse (e.g. an option forbids partial upgrades), with the reason set via
     set_error ("msg\nhint"). Called while planning, after options are known. */
  int (*can_upgrade_packages)(void* ud);
} repd_packages_vt;

typedef struct repd_services_vt {
  int (*probe)(void* ud);
  repd_status (*state)(void* ud, const char* unit, const char* scope, int* enabled,
                       int* active); /* -1 = unit unknown */
  repd_status (*enable)(void* ud, const char* unit, const char* scope);
  repd_status (*disable)(void* ud, const char* unit, const char* scope);
  repd_status (*start)(void* ud, const char* unit, const char* scope);
  repd_status (*stop)(void* ud, const char* unit, const char* scope);
} repd_services_vt;

typedef enum { REPD_PRIV_USER = 0, REPD_PRIV_SYSTEM = 1 } repd_privilege;

typedef struct repd_registrar_api {
  int (*register_packages)(repd_registrar* r, const char* name, const char* version,
                           repd_privilege priv, const repd_packages_vt* vt, void* ud);
  int (*register_services)(repd_registrar* r, const char* name, const char* version,
                           const repd_services_vt* vt, void* ud);
  /* Declares an option users set with rp.option(key, v) or --option key=v.
     key is "<backend>.<name>" (e.g. "yay.review"); values is a comma-separated
     list of allowed values (first-class validation by repd); default_value is
     one of them. */
  int (*declare_option)(repd_registrar* r, const char* key, const char* values,
                        const char* default_value);
} repd_registrar_api;

/* Exported by the plugin. Return 0 on success. If abi != REPD_PLUGIN_ABI the
   plugin MUST return non-zero without registering; repd also refuses it. */
REPD_PLUGIN_EXPORT int repd_plugin_init(int abi, const repd_host* host, const repd_host_api* api,
                                        repd_registrar* reg, const repd_registrar_api* reg_api);
/* Optional: exported for diagnostics before init. */
REPD_PLUGIN_EXPORT int repd_plugin_abi(void);

#ifdef __cplusplus
}
#endif
