# repro Lua API reference (lua-api 1)

This is the normative, user-facing reference for configuring `repro`. Every
public `rp.*` symbol has a heading here; a test (`tests/lua/test_api_docs.lua`)
fails if one is missing. Configuration is plain Lua 5.1 (LuaJIT).

- [Repository layout](#repository-layout)
- [Phases and the sandbox](#phases-and-the-sandbox)
- [Root configuration (`repro.lua`)](#root-configuration-reprolua)
- [Modules and interfaces](#modules-and-interfaces)
- [Read-only queries](#read-only-queries)
- [Backend facades](#backend-facades)
- [Actions and `ctx`](#actions-and-ctx)
- [Repo-local backends](#repo-local-backends)
- [Errors](#errors)

## Repository layout

```
~/.config/repro/            (or --config PATH, or $REPRO_CONFIG)
├── repro.lua               entry point: options, pins, profiles
├── repro.lock.lua          generated: recorded provider choices (commit it)
├── modules/                the path is the module name
│   ├── base/
│   │   ├── init.lua        -> module "base"
│   │   └── files/          sources for base's `files` / `udev` entries
│   ├── dev/ide/init.lua    -> module "dev/ide"
│   └── dev/ide/vim.lua     -> module "dev/ide/vim" (single file: no sources)
└── plugins/lua/            repo-local backends (reduced trust)
```

Module names match `^[a-z0-9_+-]+(/[a-z0-9_+-]+)*$`. `modules/x.lua` and
`modules/x/init.lua` must not both exist. A directory module keeps the files
it deploys in its own `files/` directory; that directory is never scanned for
modules, so it may hold `.lua` files (e.g. an nvim config).

## Phases and the sandbox

| Phase | When | Changes allowed? |
|---|---|---|
| EVAL | loading `repro.lua`, modules, `with_params`, repo backends' top level | no |
| CHECK | step checks and action `check(ctx)` | no |
| APPLY | applying steps and action `run(ctx)` | yes, via `ctx` only |

Every config file runs in a fresh environment containing only `rp`, `string`,
`table`, `math` (without `random`/`randomseed`), `ipairs`, `pairs`, `next`,
`select`, `type`, `tostring`, `tonumber`, `error`, `assert`, `pcall`,
`unpack`, `setmetatable`, `getmetatable`, `rawget`, `rawset`, `rawequal`.
`io`, `os`, `require`, `load*`, `dofile`, `debug`, `ffi`, `jit`, `package` and
`collectgarbage` are absent. Reading an undefined global is an error, with a
suggestion when it looks like an `rp.*` name (`module` → `rp.module`).
`rp` itself is read-only.

### `rp.api_version`

Integer, currently `1`. Additive changes keep the number; removals or
semantic changes bump it.

## Root configuration (`repro.lua`)

These builders may only be called from `repro.lua`.

### `rp.option(key, value)`

| Key | Values |
|---|---|
| `"packages.default_backend"` | the default backend for unprefixed `packages` entries (e.g. `"pacman"`). Unset by default, and there is no `"auto"`: without it every package needs a `backend:` prefix |
| `"<add-on>.<name>"` | options declared by installed add-ons, e.g. `"yay.review"`. Values are checked against the add-on's list; a key for an add-on that is not installed on this machine is ignored, so one repository serves machines with different add-ons |
| `"services.backend"` | `"auto"` (default: the first usable backend, ranked system add-ons > user add-ons > repo Lua backends, then by name) or a backend name |
| `"env.targets"` | `"auto"` (default) or a list of `"sh"`, `"fish"`, `"environment.d"` — see [Environment variables](#environment-variables) |

```lua
rp.option("packages.default_backend", "pacman")
rp.option("env.targets", { "fish", "sh" })
```

**Several package managers**: each `packages` entry names its backend with a
prefix, and unprefixed entries go to the default backend set above. A module
may mix backends; each gets its own step, run in that backend's pass:

```lua
rp.option("packages.default_backend", "pacman")          -- repro.lua
-- in a module:
packages = { "neovim", "yay:paru-bin", "flatpak:org.mozilla.firefox" }
```

**One install per backend**: repro collects the packages of all modules in the
run and installs them through each backend in one go (one `pacman -S`, one
`yay -S`, …), in the plan under the first module that uses the backend and
listing the modules it installs for:

```
  apps/browser/firefox 1 for system pass
    · packages pacman: install firefox, helium-browser, mpv, …  (for apps/browser/firefox, apps/media, …)  [system pass]
```

`requires` still holds: a module whose dependencies have other steps in the
same pass (files, services, actions, …) after that point gets a new install
there, so e.g. `pacman.conf` is written before packages from the repository it
adds. A package two modules declare is installed once and recorded for the
first of them; `repro status` shows a module pending while one of its packages
is missing, whichever module's install carries it.

Installing never refreshes the package databases first (on Arch that would be
a partial upgrade). Run `repro sync --upgrade` to fully upgrade each packages
backend in use (`pacman -Syu`, `yay -Syu`, …) before its installs; the upgrade
shows in the plan and runs in that backend's pass.

`repro sync --upgrade MODULE…` upgrades only the packages those modules
declare (plus their optional modules with `MODULE+`, not the modules they
require), with one upgrade per backend for all of them. Packages not installed
yet are simply installed. What a backend does:

| Backend | `--upgrade MODULE…` |
|---|---|
| `pacman` | refused by default: refreshing the databases and upgrading single packages is a partial upgrade. With `rp.option("pacman.partial", "allow")` (or `--option pacman.partial=allow`) it runs one `pacman -Sy --needed -- <packages>`; pacman still refuses anything that breaks a declared dependency, but undeclared ones (glibc symbol versions, Python minor versions) are your risk |
| `yay` | `yay -S --needed` without a refresh: AUR packages are rebuilt when the AUR has a newer version, repo packages move only to what the last full upgrade fetched |
| `flatpak`, `flatpak-user` | `flatpak update <apps>` |

A prefix naming a backend that is not installed is `E_NO_BACKEND` with the
add-on to install; an unprefixed entry without a default is `E_CONFIG`.
`rp.packages.installed { ... }` and friends accept the same spellings.

**AUR packages** come from the `repro-yay` add-on (`"yay:paru-bin"`, or make
`yay` the default backend). yay installs repo and AUR packages
alike, refuses to run as root, and so installs during `repro sync -u`. It runs
on your terminal: you see the build output, and when sudo needs a password,
repro asks for it first (`[repro] sudo password for you (yay installs
packages):`). Without a terminal the step fails unless sudo credentials are
already cached (`sudo -v`) or sudo needs no password.

AUR build scripts are reviewed by default: with `yay.review = "diff"` yay's
diff menu is on, so it shows each AUR package's PKGBUILD (only the changes on
updates) and asks before building — which needs a terminal. `"skip"` builds
without review and without confirmations, e.g. for unattended runs:

```lua
rp.option("yay.review", "skip")          -- repro.lua, for every run
```
```sh
repro sync -u --option yay.review=skip   # this run only
```

**Flatpak apps** come from the `repro-flatpak` add-on, which has one backend
per installation: `"flatpak:org.mozilla.firefox"` installs system-wide during
`sudo repro sync -s`, `"flatpak-user:org.mozilla.firefox"` into your home
(`flatpak --user`) during `repro sync -u`. Entries are application IDs or full
refs (`"flatpak:runtime/org.freedesktop.Platform/x86_64/24.08"`). flatpak looks
an ID up in the installation's remotes, so add one first, e.g. in a Lua action
or once by hand:

```sh
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
```

`repro sync --upgrade` runs `flatpak update` for each installation in use;
`repro sync --upgrade MODULE…` updates just those modules' apps.

Any option can be overridden per run with `--option KEY=VALUE` on `sync`,
`status` and `show`.

### `rp.pin(interface, module)`

Repo-wide provider choice for an interface (precedence level 3).

### `rp.profile { name, description?, default?, use, providers? }`

A named selection of modules. At most one profile has `default = true`; it is
used when `--profile` is not given. `use` entries are module names or
`{ "name", params = { ... } }`; `"name+"` also selects the module's optional
requires (see [Optional modules](#optional-modules-and-integrations)), and
`{ "name", auto_sync = false }` does not select the module but keeps an
integration from joining in this profile (`auto_sync = true` lets one in).
`providers` overrides interface choices for this profile (precedence level 2).

```lua
rp.profile {
  name = "laptop", default = true,
  use = { "base", { "terminal", params = { font_size = 14 } } },
  providers = { ["dev/ide"] = "dev/ide/vim" },
}
```

Provider precedence: `--provider` (1) › profile (2) › `rp.pin` (3) ›
`repro.lock.lua` (4, unless `--reselect`) › the only provider › an
interactive prompt (recorded in the lock) › error `E_AMBIGUOUS`.

## Modules and interfaces

A module file returns exactly one declaration.

### `rp.module { ... }`

| Field | Type | Meaning |
|---|---|---|
| `name` | string | optional; must equal the path-derived name |
| `description` | string | shown by `repro list modules` |
| `provides` | interface name | this module can satisfy that interface |
| `priority` | integer | lower is preferred (prompt default); default 100 |
| `requires` | list | modules or interfaces applied first; `{ "name", optional = true }` is only listed (see [Optional modules](#optional-modules-and-integrations)) |
| `integrates` | list of names | makes this an integration: it joins once all of these are synced |
| `auto_sync` | boolean | default `true`; `false` keeps an integration out until it is named |
| `params` | map | parameters (see `rp.param`) |
| `with_params` | `function(p, sys)` | returns extra content computed from params |
| `packages` | list of strings | `"name"` (default backend) or `"backend:name"`, e.g. `"flatpak:org.mozilla.firefox"` |
| `files` | list of file entries | see below |
| `env` | map | variables for the target user's sessions (see [Environment variables](#environment-variables)) |
| `system_env` | map | variables for every user (see [Environment variables](#environment-variables)) |
| `services` | list | `{ unit, scope = "system"|"user", state = "enabled"|"disabled", active? }` |
| `users` | list | `{ name, shell?, groups? }` — created or extended, never removed |
| `locale` | table | `{ locales = { "en_US.UTF-8 UTF-8" }, default? = "en_US.UTF-8" }` |
| `udev` | list | `{ name = "70-x.rules", src | content }` |
| `actions` | list | `rp.action { ... }` (imperative escape hatch) |

Unknown fields are errors with the closest valid name suggested.

**Services** are handled by the selected services backend: `repro-openrc` or
`repro-systemd`, auto-picked by the init the host booted. Units are written in
systemd style, so a module works on both: under OpenRC, `"sshd.service"` means
the `sshd` init script, and a unit type with no OpenRC counterpart (`.timer`,
`.socket`, …) is an error. With OpenRC, a service counts as `enabled` when it is
in any runlevel (so `elogind` in `boot` is left alone); enabling adds it to
`default`, and disabling removes it from every runlevel. `scope = "user"` uses
OpenRC user services (0.60 or newer).

**File entries**: `{ dest, src | content, action?, mode?, scope?, adopt? }`.
`dest` is absolute or `~/…`. `src` is relative to the module's own `files/`
directory (`modules/<name>/files/`) and must stay inside it; only directory
modules (`modules/<name>/init.lua`) can use `src`. `action` is `link` (default with `src`), `copy`, or `create`
(default with `content`). `mode` is an octal string (`"0600"`), not allowed on
links. Files under the home directory belong to the user pass, others to the
system pass; `scope = "user"` keeps a non-home file in the user pass.
`adopt = true` lets repro take over an existing unmanaged file (a backup
`<dest>.repro-adopted-<timestamp>` is kept); otherwise such files are refused
until you pass `--adopt`. Drift on files repro manages is restored silently.

**Directories**: `src` may name a directory. With `copy`, every file under it
is copied to the same relative path under `dest` and managed on its own: drift,
adoption and updates work per file, executable files stay executable (unless
`mode` is set, which then applies to every file), and files that only exist at
the destination are left alone. That suits a neovim config, where the plugin
manager writes `lazy-lock.json` next to your files:

```lua
-- modules/nvim/init.lua, with the config in modules/nvim/files/nvim/
return rp.module {
  files = { { dest = "~/.config/nvim", src = "nvim", action = "copy" } },
}
```

With `link` (the default), `dest` becomes one symlink to the whole directory,
so edits in either place are the same file. An existing real directory at
`dest` is refused until you adopt it, which moves it aside as
`<dest>.repro-adopted-<timestamp>`. Switching an entry from `link` to `copy`
is refused while `dest` is still the old symlink; remove the symlink first.
A file removed from the source directory is not deleted from `dest`. A source
symlink to a file is copied as a file, but a symlink to a directory is an error.

`with_params` runs during EVAL with the resolved, read-only params; the
returned table may contain only content fields. Params (and `ctx.params`) read
like ordinary tables — `#`, `ipairs`, `pairs`, `next`, `table.concat`,
`unpack` — and can be passed straight into the declaration
(`locale = { locales = p.locales }`). Assigning to them, or `table.insert`,
`table.remove` and `table.sort` on them, is an `E_CONFIG` error; copy first
(`local l = { unpack(p.locales) }`). Lists are concatenated and
de-duplicated, maps merged; the same key with two values is `E_CONFLICT`.

### Optional modules and integrations

A `requires` entry marked optional is listed, not synced:

```lua
-- modules/dev/rust/init.lua
return rp.module {
  requires = { "base", { "dev/rust/analyzer", optional = true } },
  packages = { "rustup" },
}
-- modules/dev/rust/analyzer.lua
return rp.module { requires = { "dev/rust" }, packages = { "rust-analyzer" } }
```

`repro sync -u dev/rust` shows `dev/rust/analyzer` under *optional, not
synced*. Name it to sync it (`repro sync -u dev/rust dev/rust/analyzer`, or in
a profile's `use`), or add `+` to the module offering it: `dev/rust+` selects
`dev/rust` with all its optional requires (one level). If a module named
`x+` exists, `x+` means that module. An optional entry orders nothing, so
`dev/rust` and its analyzer may name each other.

An **integration** connects modules that are useful together. It joins the run
by itself once every module (or interface) it `integrates` is being synced, and
is applied after them:

```lua
-- modules/shell/fish/rust.lua
return rp.module {
  integrates = { "shell/fish", "dev/rust" },
  files = { { dest = "~/.config/fish/conf.d/rust.fish", src = "rust.fish" } },
}
```

The plan lists the integrations touching the run: `active`, `inactive, needs
shell/fish`, or held by `auto_sync = false`. Naming an integration syncs it
together with what it integrates. With `auto_sync = false` (in the module, or
`{ "shell/fish/rust", auto_sync = false }` in a profile) an integration waits
until it is named. A plain (non-optional) `requires` always syncs the module
it names, whatever its `auto_sync`. `repro list modules` shows each module's
`integrates`, `optional for` and `auto_sync = false`.

### `rp.interface { name?, description }`

A role that provider modules fill (`provides = "<interface>"`). Interfaces
carry no content, params or requires.

### `rp.param { default?, type?, one_of?, description? }`

Declares a parameter. `type` is `string`, `number`, `boolean`, `list` or
`map`, inferred from `default` when omitted. A plain value in `params` is
shorthand for `rp.param { default = value }`. Values are validated against
`type` and `one_of` (`E_PARAM` names the module, parameter, value and allowed
values). A parameter without a default must be set by the profile.

### `rp.action { name, mode, check, run }`

An imperative step, shown as `[imperative]` in plans and journals. `mode` is
`"user"` or `"system"`. `check(ctx)` returns `true` when nothing needs doing;
it must not change anything. `run(ctx)` does the work. Checks are skipped
while the module's fingerprint is unchanged (use `--refresh MODULE` to force).

## Read-only queries

Available in every phase. Paths are absolute or start with `~/`. Results feed
into the module's fingerprint, so adapting to the machine stays deterministic
per machine state.

### `rp.fs.exists(path)`

`true` if anything exists at `path` (symlinks are not followed).

### `rp.fs.read(path)`

File content as a string (≤ 1 MiB), or `nil` when it is not a regular file.

### `rp.fs.stat(path)`

`{ type = "file"|"dir"|"link"|"other", mode, size, uid, gid }` or `nil`.

### `rp.fs.list(dir)`

Sorted list of names in `dir` (empty if it does not exist).

### `rp.sys.hostname()`

The machine's host name.

### `rp.sys.user()`

The target user's name (`$SUDO_USER` under sudo).

### `rp.sys.home()`

The target user's home directory.

### `rp.sys.arch()`

Machine architecture, e.g. `"x86_64"`.

### `rp.sys.os_release()`

`/etc/os-release` as a table, e.g. `{ ID = "arch", NAME = "Arch Linux" }`.

## Backend facades

Calls go to the selected backend for the namespace (missing backend:
`E_NO_BACKEND`, naming the add-on to install).

### `rp.packages.installed(names)`

`{ [name] = true|false }`. Read-only; allowed while configuration is read.

### `rp.packages.install(names)`

APPLY only. Declare `packages = { ... }` instead; calling it while
configuration is read raises `E_DECLARATIVE`.

### `rp.packages.remove(names)`

APPLY only. repro v1 never removes packages on its own.

### `rp.packages.refresh()`

APPLY only. repro refreshes package databases itself before installing.

### `rp.services.state(unit, scope?)`

`{ enabled = bool, active = bool }`, or `nil` for an unknown unit.

### `rp.services.enable(unit, scope?)`

APPLY only; declare `services = { ... }` instead.

### `rp.services.disable(unit, scope?)`

APPLY only.

### `rp.services.start(unit, scope?)`

APPLY only.

### `rp.services.stop(unit, scope?)`

APPLY only.

## Actions and `ctx`

`ctx` (frozen for lua-api 1):

| Field | Meaning |
|---|---|
| `ctx.mode` | `"user"` or `"system"` |
| `ctx.home`, `ctx.user` | target user |
| `ctx.params` | the module's resolved params (read-only) |
| `ctx.providers` | interface → provider (read-only) |
| `ctx.fs.exists/read/stat/list` | as `rp.fs` |
| `ctx.exec(cmd)` | APPLY only: `/bin/sh -c cmd` → `{ code, stdout, stderr }`; output tail is journaled. Calling it from `check` is `E_DECLARATIVE`. |
| `ctx.log(msg)` | appended to the step's journal detail |

```lua
actions = {
  rp.action {
    name = "install rustup", mode = "user",
    check = function(ctx) return ctx.fs.exists("~/.cargo/bin/rustup") end,
    run = function(ctx)
      local r = ctx.exec("curl -sSf https://sh.rustup.rs | sh -s -- -y")
      if r.code ~= 0 then error("rustup failed: " .. r.stderr) end
    end,
  },
}
```

## Repo-local backends

### `rp.backend { namespace, name, privilege?, ops }`

A Lua file in `plugins/lua/` returning a backend. It is flagged
`repo ⚠ (reduced trust)` in previews and `repro plugins list`.

```lua
return rp.backend {
  namespace = "packages",            -- or "services"
  name = "mypm",
  privilege = "system",              -- packages only: which pass owns its steps
  ops = {
    probe     = function(ctx) return true end,
    installed = function(ctx, names) return { [name] = bool, ... } end,
    install   = function(ctx, names) ... end,
    -- optional: remove, refresh, upgrade(ctx) (repro sync --upgrade),
    --           upgrade_packages(ctx, names) (repro sync --upgrade MODULE…)
    -- services: state(ctx, unit, scope), enable/disable/start/stop(ctx, unit, scope)
  },
}
```

Backend `ctx` adds `ctx.mem.fetch(key, default)` and
`ctx.mem.update(key, names, value)` — a small persistent scratch store kept in
the state directory (intended for test doubles). `ctx.exec` and
`ctx.mem.update` are APPLY-only.

## Environment variables

`env` and `system_env` are written once per module and per *target*, each in
the drop-in location its consumer already reads, so no init system or shell
is assumed:

| Target | `env` (user) | `system_env` |
|---|---|---|
| `sh` | `~/.local/share/repro/env.d/<module>.sh`, loaded by one managed block in the login profile (`~/.profile`; `~/.bash_profile` for bash if it exists; `~/.zprofile` for zsh) | `/etc/profile.d/repro-<module>.sh` |
| `fish` | `~/.config/fish/conf.d/repro-<module>.fish` | `/etc/fish/conf.d/repro-<module>.fish` |
| `environment.d` | `~/.config/environment.d/90-repro-<module>.conf` | `/etc/environment.d/90-repro-<module>.conf` |

With `"auto"`, user variables follow the target user's login shell (`fish` →
fish, anything else → sh); system variables always get `sh`, plus `fish` when
fish is installed; both get `environment.d` when systemd is the running init.
Values are quoted for each consumer. Module names map to file names with `/`
replaced by `--`. The same variable declared by two modules in one scope is an
`E_CONFLICT`.

## Errors

Errors print as `error[E_CODE]: <what, naming the module/step/path>` followed
by `  hint: <next action>`. Exit codes: `0` success (including dry runs and
up-to-date runs), `1` a step failed or was blocked by the adoption guard, `2`
configuration/plan/usage error, `130` interrupted. The full catalogue is in
`specs/001-repro-core-mvp/contracts/cli.md`.
