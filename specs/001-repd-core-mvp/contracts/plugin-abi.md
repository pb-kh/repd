# Contract: native backend plugin C ABI (`REPD_PLUGIN_ABI = 1`)

Header shipped as `include/repd/plugin.h` and installed for add-on authors.
Pure C, no C++ types cross the boundary; all strings are NUL-terminated UTF-8.

```c
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
  int  (*exec)(const repd_host *h, const char *const *argv,
               char **out, char **err);         /* out/err freed with free_str */
  void (*log)(const repd_host *h, const char *msg);
  /* Message for failures; an optional second line ("msg\nhint") is the hint. */
  void (*set_error)(const repd_host *h, const char *msg);
  void (*free_str)(char *s);
  int  test_mode;                                /* 1 in -Dtest_hooks builds */
  /* Run argv (no shell) on repd's terminal: stdin/stdout/stderr inherited,
     nothing captured; repd prints "  $ <argv>" to stderr first and journals
     the command line and exit status. For tools that must talk to the user,
     e.g. sudo's password prompt. Returns the exit status (127: cannot start). */
  int  (*exec_tty)(const repd_host *h, const char *const *argv);
  /* Effective value of a declared option: --option, else rp.option, else the
     default; NULL if undeclared. */
  const char *(*option)(const repd_host *h, const char *key);
} repd_host_api;

typedef struct repd_packages_vt {
  int (*probe)(void *ud);                                     /* 1 = usable on this system */
  repd_status (*installed)(void *ud, const char *const *names, size_t n,
                            unsigned char *out_flags);        /* out_flags[i] = 0/1 */
  repd_status (*install)(void *ud, const char *const *names, size_t n);
  repd_status (*remove)(void *ud, const char *const *names, size_t n);
  repd_status (*refresh)(void *ud);
  repd_status (*upgrade)(void *ud);  /* optional: full upgrade, repd sync --upgrade */
  /* optional: targeted upgrade, repd sync --upgrade MODULE… (one call per run) */
  repd_status (*upgrade_packages)(void *ud, const char *const *names, size_t n);
  /* optional (NULL = always): 1 = upgrade_packages may run; 0 = refused, reason via set_error */
  int (*can_upgrade_packages)(void *ud);
} repd_packages_vt;

typedef struct repd_services_vt {
  int (*probe)(void *ud);
  repd_status (*state)(void *ud, const char *unit, const char *scope,
                        int *enabled, int *active);           /* -1 = unit unknown */
  repd_status (*enable)(void *ud, const char *unit, const char *scope);
  repd_status (*disable)(void *ud, const char *unit, const char *scope);
  repd_status (*start)(void *ud, const char *unit, const char *scope);
  repd_status (*stop)(void *ud, const char *unit, const char *scope);
} repd_services_vt;

typedef enum { REPD_PRIV_USER = 0, REPD_PRIV_SYSTEM = 1 } repd_privilege;

/* Registration entry points are passed in (not exported by the repd
   executable), so the binary needs no -rdynamic and plugins link against nothing. */
typedef struct repd_registrar_api {
  int (*register_packages)(repd_registrar *r, const char *name, const char *version,
                           repd_privilege priv, const repd_packages_vt *vt, void *ud);
  int (*register_services)(repd_registrar *r, const char *name, const char *version,
                           const repd_services_vt *vt, void *ud);
  /* Option "<backend>.<name>", allowed values (comma-separated), default. */
  int (*declare_option)(repd_registrar *r, const char *key, const char *values,
                        const char *default_value);
} repd_registrar_api;

/* Exported by the plugin. Return 0 on success. If abi != REPD_PLUGIN_ABI the
   plugin MUST return non-zero without registering; repd also refuses it. */
REPD_PLUGIN_EXPORT int repd_plugin_init(int abi, const repd_host *host, const repd_host_api *api,
                      repd_registrar *reg, const repd_registrar_api *reg_api);
/* Optional: exported for diagnostics before init. */
REPD_PLUGIN_EXPORT int repd_plugin_abi(void);

#ifdef __cplusplus
}
#endif
```

## Loading rules

1. Search dirs (in order): the system plugin directory, fixed at build time
   with `-Dplugin_dir` (macro `REPD_PLUGIN_DIR`, default `/usr/lib/repd`;
   e.g. `/usr/lib64/repd` on Fedora), then in user mode only
   `~/.local/lib/repd`. Only files named `librepd-<name>.so` are loaded,
   sorted by name; anything else in those directories is ignored.
2. `dlopen(path, RTLD_NOW | RTLD_LOCAL)`; missing `repd_plugin_init` or
   `repd_plugin_abi() != REPD_PLUGIN_ABI` → plugin refused, reported as
   `error[E_PLUGIN_ABI]: <path> built for plugin-abi X, this repd supports 1`.
   Fail-soft: other plugins continue loading.
3. Registration duplicates (same namespace+name) → first in search order wins,
   the duplicate is reported in `plugins list`.
4. Vtable functions are only called from the main thread, in CHECK (probe,
   installed, state) and APPLY (install/remove/refresh/enable/…) phases.
5. A plugin MUST NOT write to stdout/stderr directly; use `api->log`. A child
   it starts through `api->exec_tty` writes to the terminal; use that only
   where the user has to see the output or answer a prompt.
6. `exec_tty`, `option`, `declare_option` and `repd_packages_vt.upgrade`,
   `upgrade_packages` and `can_upgrade_packages` were
   appended before the first release, so they are part of plugin-abi 1; later additions bump
   `REPD_PLUGIN_ABI`.
7. Options: repd validates `rp.option` / `--option` values against the
   declared list before any plugin call. A repd.lua key whose prefix is not an
   installed backend is ignored; an undeclared key for an installed one, or any
   unknown `--option` key, is `E_CONFIG`.

## `plugins list` output

```
NAMESPACE  NAME     VERSION  ABI  ORIGIN   PATH
packages   pacman   1.0.0    1    system   /usr/lib/repd/librepd-pacman.so
services   systemd  1.0.0    1    system   /usr/lib/repd/librepd-systemd.so
packages   fake     lua      -    repo ⚠   ~/.config/repd/plugins/lua/fake-packages.lua (reduced trust)
refused:
  /usr/lib/repd/librepd-old.so  plugin-abi 0 ≠ 1
```

## First-party add-ons in this repo (separate deliverables)

Installed into the plugin directory (`-Dplugin_dir`). Each add-on has its own
install tag, so it can be installed without the core:
`meson install -C build --tags pacman` (or `systemd`, `openrc`, `yay`, `flatpak`).

| Add-on | Namespace | Privilege | Implementation notes |
|---|---|---|---|
| `repd-pacman` → `librepd-pacman.so` | packages | system | probe: `/usr/bin/pacman` exists; installed: `pacman -T -- names…` (deptest: prints the unsatisfied names, exit 127; honours provides, so `yay` is satisfied by `yay-bin`); install: `pacman -S --needed --noconfirm -- names…` (never a bare `-Sy` first: that would be a partial upgrade); upgrade: `pacman -Syu --noconfirm` via `exec_tty`; option `pacman.partial` = `refuse` (default) | `allow`; can_upgrade_packages: 1 only with `allow`, else the reason (a partial upgrade) and the hint to upgrade everything or `--option pacman.partial=allow`; upgrade_packages: `pacman -Sy --needed --noconfirm -- names…` via `exec_tty`; refresh: `pacman -Sy`, only for an explicit `rp.packages.refresh()` |
| `repd-systemd` → `librepd-systemd.so` | services | system/user | `systemctl [--user] is-enabled/is-active/enable/disable/start/stop -- unit`; user scope under sudo is refused (user services belong to the user pass); probe: `systemctl` exists and the host did not boot OpenRC (`/run/openrc` without `/run/systemd/system`) |
| `repd-openrc` → `librepd-openrc.so` | services | system/user | probe: `/run/openrc` is a directory, or `rc-service` exists and `/run/systemd/system` does not; unit `x.service` means service `x`, other systemd unit types (`.timer`, `.socket`, …) are refused; exists: `rc-service [--user] --exists`; enabled = listed in any runlevel by `rc-update [--user] --nocolor show`; active = `rc-service [--user] --nocolor svc status` exits 0; enable: `rc-update [--user] add svc default`; disable: `rc-update [--user] --all del svc`; start/stop: `rc-service [--user] --nocolor svc start/stop`; user scope (OpenRC ≥ 0.60 user services) under sudo is refused |
| `repd-yay` → `librepd-yay.so` | packages (repo + AUR) | **user** (yay refuses root; its packages belong to the user pass) | probe: `/usr/bin/yay` exists; installed: `pacman -T` as for pacman; option `yay.review` = `diff` (default) | `skip`; install: `yay -S --needed` + review flags `--diffmenu --noanswerdiff --answerclean None --answeredit None` (yay shows PKGBUILDs/diffs and asks; without a terminal the step fails, hinting `--option yay.review=skip`) or skip flags `--noconfirm --answerclean None --answerdiff None --answeredit None`, then `--sudoloop -- names…`, via `exec_tty`; upgrade: `yay -Syu` with the same review/skip flags; upgrade_packages: `yay -S --needed` (as install, no refresh: AUR packages rebuild if newer, repo packages stay at the last full upgrade's versions); no refresh; remove: `yay -R …` likewise. Before running yay: if yay's `sudobin` (`yay -Pg`) is `sudo` and `sudo -n true` fails, a terminal is required — then `sudo -v -p "[repd] sudo password for %u (yay installs packages): "` runs via `exec_tty`, otherwise the step fails with a hint to run in a terminal or `sudo -v` first. Other `sudobin`s (doas) prompt through yay on the terminal |
| `repd-flatpak` → `librepd-flatpak.so` | packages (two backends: `flatpak`, `flatpak-user`) | `flatpak`: system (`--system`); `flatpak-user`: **user** (`--user`, refused as root) | one backend per installation, because privilege decides the pass; `ud` carries the installation. Entries are application IDs or full refs (`app/…`/`runtime/…` kind optional). probe: `/usr/bin/flatpak` exists; installed: `flatpak list --system|--user --columns=application,ref` (apps and runtimes; a name matches the application or the ref); install: `flatpak install --system|--user --noninteractive -y -- names…` via `exec_tty` (no remote: flatpak searches the installation's remotes; on failure the hint shows `flatpak remote-add … flathub`); upgrade: `flatpak update --system|--user --noninteractive -y` via `exec_tty`; upgrade_packages: the same with `-- names…`; no refresh; remove: `flatpak uninstall` likewise. A missing `flatpak-user` backend names the `repd-flatpak` add-on |
