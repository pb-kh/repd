# repro — Requirements

**Status**: draft (pre-specification, gathered against Constitution v1.0.0)
**Input sources**: project brief from the user, `.specify/memory/constitution.md`, requirements Q&A session (2026-10-09).

---

## 1. Purpose & Vision

`repro` is a NixOS-like, declarative environment tool for (Arch-style) Linux systems:
the user declares the desired state of their machine in **one place** — a folder of
Lua modules and supporting config files — and `repro` applies those declarations.

In contrast to Nix/home-manager, `repro` manages the *existing* system in place
(packages via the distro package manager, dotfiles in `$HOME`, system files under
`/etc`) rather than building a separate profile world.

The product stands on three pillars:

1. **Declarative modules** — the unit of configuration is a *module* (e.g. `dev/c++`)
   that declares intent (packages, files, env vars, services…). Modules can be
   applied **partially** (`repro sync` a subset, or only user-vs-system halves).
2. **Composability** — modules declare dependencies on other modules, and on
   *interfaces* that may have several **providers** (e.g. `dev/ide` is provided by
   `dev/ide/vim`, `dev/ide/emacs`, …). Provider choice is asked once and recorded.
3. **Extensible core** — the `repro` binary is a thin C++23/LuaJIT host. Namespace
   implementations (`rp.packages`, `rp.services`, …) are plugins: shared objects
   (`pacman`, `dnf`, `nix`; `systemd`, `openrc`) shipped as separate packages
   (e.g. `pacman -S repro repro-pacman repro-openrc`), or pure-Lua small backends.

All state necessary to avoid redoing work (per-module decision hashes, per-step
results) is recorded in a **SQLite state database** (one per privilege mode), so a
re-run of `repro sync` only applies what is missing or stale.

## 2. Constitution Conformance Summary

| Constitution principle | How these requirements honor it |
|---|---|
| I. Declarative & Reproducible | Eval phase only *records intents*; apply is idempotent; plan is a pure function of (config + declared params + recorded choices); deterministic ordering everywhere; `--dry-run` everywhere. |
| II. Lua/LuaJIT surface | All configuration is Lua. The `rp.*` API is the public contract; C++ never parses config. |
| III. C++23 + Meson | Core is C++23, Meson/Ninja only; LuaJIT and SQLite are pinned subprojects. |
| IV. Test-First | Every FR below is testable; tests are registered in `meson test`; integration tests run against sandboxed fixtures (fake backends, scratch `$HOME`). |
| V. Safe/Idempotent/Observable | Preview, idempotence, resumable journal, adoption guard for pre-existing files, structured state DB journal. |

Note on the single-binary constraint: the *core* remains one self-contained binary.
Backend `.so` plugins are the explicitly managed extension mechanism (analogous to
the constitution's allowance for the package managers it manages), NOT core
dependencies; `repro` must still start and report a clear error with zero plugins.

## 3. Glossary

- **Module** — a Lua file under `modules/` matching `repro's` module contract; the
  unit of selection, planning, hashing and state.
- **Interface** — a module-like declaration (`rp.interface{}`) that other modules can
  *require*; it has no content of its own.
- **Provider** — a module (`rp.module{}`) with `provides = "<interface>"`.
- **Profile** — a named selection of modules (+ params + provider overrides) defined
  in `repro.lua`; picked per-run via `--profile` (default profile exists).
- **Mode** — privilege context of a run: **user** (`-u`) or **system** (`-s`).
- **Step** — the smallest apply unit (install package `clang`, link file, enable
  unit…). Steps belong to a module; a step has a privilege level.
- **Decision hash** — sha256 over everything that determines a module's plan
  (source bytes, params, resolved providers, backend, versions). Used for skipping.
- **Backend** — a plugin implementation behind a pluggable namespace
  (`packages`, `services`, …).
- **Lock** — `repro.lock.lua`, a repo-local, generated Lua file recording provider
  decisions and where they came from.

## 4. System Overview

```
                       ┌────────────────── user's config repo ──────────────┐
                       │  repro.lua   repro.lock.lua   modules/   files/    │
                       │  plugins/lua/*.lua                                 │
                       └────────────────────────┬───────────────────────────┘
                                                │ 1. discover & eval (LuaJIT, recording only)
   /usr/lib{,64}/repro/plugins/*.so ── load ─▶  │ 2. resolve modules + interfaces/providers
                                                │ 3. plan steps (pure, deterministic)
                                                │    ├─ required privilege per step
                                                │    └─ decision hash per module
        repro sync -u ──── dry-run? ────────────┤
        (user steps → ~/.local/state/repro.db)  │ 4. apply / answer providers
        sudo repro sync -s                      │ 5. journal each step, roll up module status
        (system steps → /var/lib/repro/db)      │    skip modules whose hash is unchanged+applied
                                                └──────────────────────────────────────────────
```

Two roles run the same binary: **user mode** (`repro sync -u`) applies only
user-privilege steps, writing its state under the user's XDG state dir; **system
mode** (`sudo repro sync -s`) applies only system-privilege steps, writing state to
`/var/lib/repro`. There is **no self-escalation**: the split is explicit (user
decision, 2026-10-09).

## 5. Functional Requirements

### 5.1 Evaluation model (E)

- **FR-E-1** The root config file is `repro.lua`; it is plain Lua 5.1/LuaJIT source evaluated once per invocation. No other declarative formats exist.
- **FR-E-2** The root config may declare: options (`rp.option`), profiles
  (`rp.profile{}`); nothing else is required for a valid config.
- **FR-E-3** Evaluation is **recording-only**: every `rp.*` builder callable in
  config context (`rp.module`, `rp.interface`, `rp.option`, …) merely collects
  tables. Any attempt to perform a mutating operation during evaluation
  (`rp.packages.install(...)` etc.) is an error with a clear message.
- **FR-E-4** Read-only system queries ARE allowed during evaluation and planning
  (`rp.packages.installed()`, `rp.fs.*` readers). Side-effect-free. This lets
  modules adapt to the current machine (e.g. only enable a service if unit exists).
- **FR-E-5** The C++ core MUST NOT parse, regex-scan or evaluate configuration.
  All config semantics live in the Lua bootstrap shipped inside the binary.
- **FR-E-6** The global environment is clean: `rp` (plus `string`-level stdlib) is
  the only added global; unknown globals that look like typos of `rp.*` are
  flagged with a hint.
- **FR-E-7** Evaluation output (the collected declaration tree) is deterministic:
  same inputs → same plan. Iteration over unordered sets is forbidden in the
  bootstrap; ordering comes from explicit lists or sorted keys.

### 5.2 Modules (M)

- **FR-M-1** A module is a single Lua file at `modules/<path>.lua` (or
  `modules/<path>/init.lua`), returning the table produced by `rp.module{...}`.
- **FR-M-2** Module names are slash-separated lowercase segments, chars
  `[a-z0-9_+-]`; e.g. `base`, `dev/c++`, `dev/ide/vim`. Name must equal the file
  location.
- **FR-M-3** A module MAY declare: `provides`, `requires`, `priority`, `params`,
  `packages`, `files`, `env`, `services`, `users`, `locale`, `udev`, `actions`,
  `modules` (sub-assemblies), `description`. Unknown fields error (typo guard).
- **FR-M-4** `requires` lists module OR interface names. A required interface is
  substituted by its resolved provider. Provider modules' own `requires` join the
  graph transitively.
- **FR-M-5** Dependency graph MUST be acyclic; cycles are errors printing the full
  path (`a → b → c → a`).
- **FR-M-6** Required names must resolve to an existing module/interface; near
  misses produce "did you mean …" hints.
- **FR-M-7** `params` are typed parameter declarations
  (`rp.param{ default=, one_of=, type=, description= }`). Overrides arrive from
  profile entries; invalid values error at plan time.
- **FR-M-8** Partial application is first-class:
  - a run applies only the steps whose privilege matches the run's mode;
  - explicit module subsets can be selected on the CLI (`repro sync -u dev/c++`);
    their `requires` closures are included automatically.
- **FR-M-9** `priority` (integer, lower = preferred) ranks same-interface
  providers when auto-resolution is used.

### 5.3 Interfaces & providers (I)

- **FR-I-1** An interface is `rp.interface{ name=~, description= }` at
  `modules/<ifname>.lua`. Interfaces have no apply content.
- **FR-I-2** A module provides an interface via `provides = "<interface>"`.
  Names must not collide (interface vs module name conflict is an error).
- **FR-I-3** Provider resolution precedence (highest first):
  1. CLI override `--provider <iface>=<module>` (full module path),
  2. profile `providers` table,
  3. root `repro.lua` pins (inherited defaults),
  4. `repro.lock.lua` recorded choice,
  5. exactly one available provider → auto-select,
  6. interactive prompt (listing providers with descriptions; default = lowest
     `priority`),
  7. non-interactive (`--yes`/no TTY) with >1 candidate → hard error listing
     candidates.
  A record of the winning rule is kept in the lock (`source` field).
- **FR-I-4** Provider choices are recorded in `repro.lock.lua` (generated, in the
  config repo) so later runs never re-ask. `repro sync` never writes the lock in
  `--dry-run`. Un-pinned choices can be re-asked with `--reselect <iface>`.
- **FR-I-5** A provider module can be used directly; in that case no ask happens
  for its interface unless something else also requires that interface.

### 5.4 Profiles (P)

- **FR-P-1** `rp.profile{ name=, description=, use={...}, providers={...},
  default=true? }` declares a named module selection in `repro.lua`.
- **FR-P-2** `use` entries are strings or `{ name=, params={...} }` tables.
- **FR-P-3** `repro sync --profile NAME` selects one profile; without it, the
  profile flagged `default = true` runs; if none flagged and profiles exist →
  error listing profiles. A `providers` table in the profile overrides pins and
  feeds the same precedence chain as FR-I-3.
- **FR-P-4** The profile name and its full module+params+provider selection feed
  the decision hash (changing profile content re-plans affected modules).

### 5.5 Action types (A)

The module fields below are the v1 vocabulary. Every generated step is
idempotent: apply = ensure the target state; already-correct state is a no-op.

- **FR-A-1 packages** — `packages = { "clang", "cmake", ... }` (strings; names
  only in v1 — no version constraints, see non-goals). Rendered as install/remove
  batches delegated to the active `packages` backend. Privilege: whatever the
  backend declares (pacman → system; nix → user).
- **FR-A-2 files** — `files = { { src= or content=, dest=, action= } }`.
  - `action`: `link` (default; symlink), `copy`, `create` (content only);
  - `src` starting with `./`/`../` resolves relative to the module file dir,
    otherwise relative to the config repo's `files/`;
  - parent dirs are created; `dest` may use `~`;
  - **adoption guard**: if the dest exists, is not owned by repro (no matching
    digest in state) and differs from the desired content → refuse; provide
    `--adopt` per-run or `adopt = true` per-entry.
- **FR-A-3 env** — `env = { EDITOR = "nvim", ... }`. User scope writes
  `~/.config/environment.d/90-repro-user.conf`; system scope writes
  `/etc/environment.d/90-repro-system.conf`. A `repro env-sh` helper prints shell
  integration snippets for shells that don't read environment.d (documented in
  README; the shell itself is never auto-edited).
- **FR-A-4 services** — `services = { { unit=, scope="system"|"user",
  state="enabled"|"disabled", active=bool? } }` delegated to the active
  `services` backend (`systemd`, `openrc`; openrc has no user scope → clear
  error). `state` is persistency; optional `active = true/false` adds a
  start/stop step.
- **FR-A-5 users (system only)** — `users = { { name=, shell=, groups={...},
  state="present" } }`. No user deletion in v1 (see non-goals).
- **FR-A-6 locale (system only)** — `locale = { locales = {...}, default = }`
  (locale-gen + `/etc/locale.conf` on locale-gen platforms).
- **FR-A-7 udev (system only)** — `udev = { { name=, src= or content= } }` →
  rule files into `/etc/udev/rules.d/` + `udevadm control --reload` after apply.
- **FR-A-8 actions (the marked imperative escape hatch)** —
  `rp.action{ name=, mode=, check=, run= }`. `check`/`run` are Lua functions
  executed AT APPLY TIME by the host, never during eval. `check` returning true
  makes the step a skip (idempotency for imperative bits). The context object
  `ctx` is the sole capability surface (see §9). Steps of a module run in fixed
  order: packages → files → env → services → users → locale → udev → actions.

### 5.6 Backends & plugins (B)

- **FR-B-1** Pluggable namespaces in v1: `packages`, `services`.
  `rp.packages` / `rp.services` are facades delegating to the active backend.
- **FR-B-2** Options select the backend: `rp.option("packages.default_backend", "auto")`;
  `auto` = detect by presence on the system (pacman → systemd etc.). Unknown
  option names error.
- **FR-B-3** Backend sources:
  - **`.so` plugins** via a small C ABI (see §9 sketch; ABI version constant;
    mismatch → refused with both versions printed). Loaded with `dlopen(RTLD_NOW|RTLD_LOCAL)`.
    Search path: `/usr/lib/repro/plugins/`, `/usr/lib64/repro/plugins/`,
    `~/.local/lib/repro/plugins/`, `<repro>/plugins/*.so`. Distributed by distro
    packages (`repro-pacman`, `repro-openrc`, …).
  - **pure-Lua plugins**: `plugins/lua/*.lua` in the config repo, returning
    `rp.backend{ namespace=, name=, ops={...} }`. Intended for experiments and
    (crucially) test fixtures.
- **FR-B-4** Backend ops for `packages`: `installed(names)→{name=bool}` (query,
  usable at eval), `install(list)`, `remove(list)`, `refresh()` (db sync;
  apply-time only). Ops for `services`: unit enable/disable/start/stop/query.
  Apply-time ops are invoked by the apply engine, not reachable from eval Lua.
- **FR-B-5** The core ships **no built-in backend**; if none is available/selectable,
  planning any packages/services step fails with a message naming the missing
  plugin package. Files/env/no-op backends-less configs must still work.
- **FR-B-6** `repro plugins list [--user|--system]` shows loaded plugins, their
  namespace+name, ABI and file path. Loading is fail-soft: one broken plugin
  disables only itself; the failure is reported and repro continues.
- **FR-B-7** Trust model (documented, not enforced in v1): anything on the plugin
  paths runs with the user/root's privileges. Distro path is trusted by
  installation; the user path is warned about in `repro plugins list` output.

### 5.7 Execution semantics (X)

- **FR-X-1 Determinism**: within a run, modules apply in dependency-topo order
  with lexicographic tie-break; steps in the fixed FR-A-8 order. Same config →
  same plan, byte-identical `--dry-run` output.
- **FR-X-2 Idempotence**: applying an applied config is a no-op; every step type
  must define its "already satisfied?" check.
- **FR-X-3 Dry-run**: `--dry-run` prints the full plan (modules, resolved
  providers, steps grouped, file diffs summary, package deltas) and terminates
  without writing the DB or the lock. Provider prompts may still occur to make
  the plan accurate (unless `--yes`, which errors on ambiguity instead).
- **FR-X-4 Failure policy**: stop at first failing step (default), report module
  and step id; exit code 1. `--keep-going` (v1.5) continues neighbor steps.
- **FR-X-5 Resumability**: state is journaled per step; a new run after a failure
  re-attempts only unfinished work (already-`ok` steps skip via their
  satisfaction checks).
- **FR-X-6 Advisory when mismatched**: `sync -u` on config with pending system
  steps prints a notice to run `sudo repro sync -s` (and vice versa). Modes
  never cross their privilege boundary.

### 5.8 State, hash, journal (S)

- **FR-S-1** State lives in per-mode SQLite databases (pinned subproject):
  - user: `~/.local/state/repro/repro.db`
  - system: `/var/lib/repro/repro.db`
  Schema version is stored and migrations are shipped in-core.
- **FR-S-2** Core tables (sketch in §10): `runs`, `steps`, `modules`,
  `providers`. Writes are transactional (WAL); a `flock` guards concurrent runs
  in the same mode; runs across modes are independent.
- **FR-S-3** **Decision hash** per module = sha256 over: module file bytes,
  transitive bytes of the module's local file dependencies,
  resolved params, resolved providers (name→provider map), the selected
  backends (namespace+name+version), repro version and Lua API version.
- **FR-S-4** Skip logic: a module whose decision hash equals the recorded one AND
  has no failed/pending steps AND no pending steps in this mode → skipped with
  `up to date`. `--refresh <module>` re-applies explicitly.
- **FR-S-5** Every step's journal row records: run id, module, step id, type,
  started/finished timestamps, status (`ok`/`skip`/`fail`), error text, and where
  meaningful the **content digest** of produced files and the backend's raw
  output tail (bounded). The run row records mode, profile, total counts, exit
  code — full audit per Constitution V (Constitution Principle V).
- **FR-S-6** `repro journal [-u|-s] [--last N]` pretty-prints the audit trail.
  `repro status` summarizes modules: `applied` / `stale` (hash changed) /
  `pending` / `failed`, per mode.
- **FR-S-7** `repro.lock.lua` is human-readable, Lua-syntax (Constitution: no
  proprietary formats), machine-regenerated, and contains provider assignments
  with timestamp + decision source. The lock is refreshed on run when prompts
  resolved; `repro sync` reproduces its own lock.
- **FR-S-8** When repro upgrades, modules whose backends/API version changed are
  replanned (the versions are inside the hash).

### 5.9 CLI (C)

- **FR-C-1** Commands (v1 surface):

```
repro init [--dir PATH]                       # scaffold config repo
repro list modules|profiles|providers|plugins
repro show <module> [--profile NAME]          # resolved declaration of one module
repro sync -u|--user | -s|--system            # REQUIRED mode flag (--user optional default)
           [MODULES...] [--profile NAME]
           [--dry-run] [--yes] [--adopt] [--refresh MODULE ...]
           [--provider IFACE=PATH ...] [--reselect IFACE ...] [--keep-going]
repro status [-u|-s] [--profile NAME]
repro journal [-u|-s] [--last N]
repro plugins list
```

- **FR-C-2** `-s` requires effective root; otherwise error with the hint
  `sudo repro sync -s`. `-u` requires identical SUDO_USER handling: when run as
  root, the config/state paths resolve to `$SUDO_USER`'s home unless
  `--config`/`REPRO_CONFIG` override (root run must find the *user's* repo).
- **FR-C-3** Module subset: `repro sync -u dev/c++` plans dev/c++ and its required
  closure only.
- **FR-C-4** Exit codes: 0 success; 1 apply failure; 2 config/plan/usage error;
  130 interrupted (Ctrl-C). All failures print the failing step id on the last line.
- **FR-C-5** Human output is colorized when TTY, plain otherwise; no machine JSON
  output in v1 (non-goal).

## 6. Non-Functional Requirements

- **NFR-1 Performance**: a fully-up-to-date `repro sync -u` completes in < 1s
  (eval + hash checks only; no backend calls except cheap installed-queries).
- **NFR-2 Size**: core binary < ~5 MB stripped; LuaJIT + SQLite pinned via Meson
  subprojects, no other mandatory deps.
- **NFR-3 Safety**: no step mutates before planning succeeded for all selected
  modules (plan-then-apply; no step executes from eval-time ordering accidents).
- **NFR-4 Portability**: every syscall-touching operation funnels through a small
  platform layer so other POSIX targets can be added later (Constitution).
  v1 targets: Arch-derived distros (primary dev target), NixOS treated as
  reference (backend: nix profile — post-v1 plugin).
- **NFR-5 Docs**: every `rp.*` API surface is documented in `docs/lua-api.md`
  alongside tests that exercise it (Constitution IV quality gate).
- **NFR-6 Secrets**: config may reference non-secret items only; secrets are out
  of scope for v1 (non-goals) — but file modes (e.g. `0600` on `files{}` entries)
  are supported so user-managed secrets aren't world-readable.

## 7. Non-goals (v1)

| Deferred | Reasoning / revisit when |
|---|---|
| Package version constraints | Backend-incompatible semantics; packages are name-based v1. |
| Package removal/prune of repro-orphaned pkgs | Needs ownership ledger UI; v2 with `repro prune`. |
| User deletion / `state="absent"` for users | Too destructive for v1 defaults. |
| Network module fetch (`repro fetch`), registries | Git + vendor copies suffice in v1. |
| Templating engine (mustache etc.) | Lua string functions cover it, generating `content = function()` or string per hash. |
| Multi-host sync from one machine run, host-groups | Profiles cover most; hosts can come later as a profile superset. |
| Secrets management (SOPS/age) | v2. |
| Transactions across steps (real rollback) | Journal-resume satisfies Constitution V ("transactional OR resumable"); resume is v1's answer. |
| Finite JSON output | v2. |
| Platform backends beyond pacman for `packages` (dnf/apt/nix) | Mechanism is ready; shipping order is pacman first (user decision). |

## 8. User-facing Lua API (normative examples)

The examples below are the **contract shape** the spec must refine (names are
proposals, shapes are normative).

### 8.1 Root config — `repro.lua`

```lua
-- ~/.config/repro/repro.lua
rp.option("packages.default_backend", "auto")        -- or "pacman", "nix", ...
rp.option("services.backend", "auto")        -- "systemd", "openrc"

rp.profile {
  name        = "laptop",
  description = "Daily driver",
  default     = true,
  use         = {
    "base",
    "dev/git",
    { "dev/c++", params = { compiler = "gcc" } },
    "dev/rust",      -- requires dev/ide; provider picked interactively or pinned
  },
  providers   = {                            -- profile-level pinings
    ["dev/ide"] = "dev/ide/vim",
  },
}

rp.profile {
  name = "server",
  use  = { "base", "dev/git", "sys/locale" },
}
```

### 8.2 Interface and provider

```lua
-- modules/dev/ide.lua
return rp.interface {
  name        = "dev/ide",
  description = "Editor environment that other dev modules can rely on",
}

-- modules/dev/ide/vim.lua  → module name "dev/ide/vim"
return rp.module {
  name      = "dev/ide/vim",   -- optional; asserted to match path
  provides  = "dev/ide",
  priority  = 10,
  description = "Neovim + friends",
  params    = { extra_plugins = rp.param { default = {}, type = "list" } },
  packages  = { "neovim", "ripgrep" },
  files     = { { src = "init.lua", dest = "~/.config/nvim/init.lua" } },
  env       = { EDITOR = "nvim" },
}
```

### 8.3 Ordinary module

```lua
-- modules/dev/c++.lua
return rp.module {
  name      = "dev/c++",
  description = "C/C++ toolchain",
  requires  = { "dev/ide", "dev/git" },
  params    = {
    compiler = rp.param { default = "clang", one_of = { "gcc", "clang" } },
  },
  packages  = { "cmake", "ninja" },       -- plus rp.param-driven conditionals:
  with_params = function(p, sys)
    if p.compiler == "gcc" then return { packages = { "gcc" } } else
      return { packages = { "clang" } } end
  end,
  env       = { MAKEFLAGS = "-j" },        -- static example; see below for dynamic env
}
```

Parameter-driven content is expressed by referencing params in table values
(`env = { CC = rp.param_value "compiler" }` — a *value reference*, still
declarative and hashable), NOT by imperative building. Exact value-reference
syntax is a spec-stage decision; the normative rule is: **module tables must
remain first-order data after param substitution** (then hashed verbatim).

### 8.4 System-scope module (root mode content)

```lua
-- modules/sys/locale.lua
return rp.module {
  name   = "sys/locale",
  locale = { locales = { "en_US.UTF-8", "de_DE.UTF-8" },
             default = "en_US.UTF-8" },
}

-- modules/sys/udev-leds.lua
return rp.module {
  name = "sys/udev-leds",
  udev = { { name = "90-leds.rules", content = [[
SUBSYSTEM=="leds", ACTION=="add", TEST=="brightness", ATTR{brightness}="0"
]] } },
  services = { { unit = "systemd-timesyncd", state = "enabled" } },
  users    = { { name = "repro-demo", shell = "/usr/bin/zsh", groups = { "video" } } },
}
```

### 8.5 Imperative escape hatch (explicit)

```lua
-- modules/dev/rust.lua
return rp.module {
  name = "dev/rust",
  requires = { "dev/ide" },
  actions = {
    rp.action {
      name  = "install rustup",
      mode  = "user",                        -- which mode owns this step
      check = function(ctx)
        return ctx.fs.exists(ctx.home .. "/.cargo/bin/cargo")
      end,
      run   = function(ctx)
        ctx.exec("curl --proto '=https' -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path")
      end,
    },
  },
}
```

`ctx` (documented, frozen): `ctx.fs.{exists,read,stat}`, `ctx.exec(cmd)`,
`ctx.{home,user,mode,params,providers}`, `ctx.log{...}`. Anything beyond `ctx`
must not be relied upon by user scripts (the host may extend within versions).

### 8.6 Pure-Lua backend (test/experiment path)

```lua
-- plugins/lua/fake-packages.lua
return rp.backend {
  namespace = "packages",
  name      = "fake",
  ops = {
    installed = function(ctx, names)
      local have = ctx.mem.fetch("fake:installed", {})
      local out = {}
      for _, n in ipairs(names) do out[n] = have[n] == true end
      return out
    end,
    install  = function(ctx, names) ctx.mem.update("fake:installed", names, true) end,
    remove   = function(ctx, names) ctx.mem.update("fake:installed", names, false) end,
    refresh  = function() end,
  },
}
```

### 8.7 `.so` plugin ABI (sketch; details = design phase)

```c
/* <repro/plugin.h>, shipped for backend authors */
#define REPRO_PLUGIN_ABI 1
typedef struct repro_registrar    repro_registrar;
typedef struct repro_backend_vt { /* installed/install/remove/refresh fn ptrs */ ... } repro_backend_vt;

/* symbol searched by repro; abi must match REPRO_PLUGIN_ABI or plugin is refused */
int repro_plugin_init(int abi_version, repro_registrar *reg);
/* registrar C API:
   repro_register_backend(reg, const char *ns, const char *name,
                          const repro_backend_vt *ops, void *userdata);      */
```

The C++ core wraps the vtable into the `rp.packages`/`rp.services` facade so Lua
never touches cdata from plugins; plugin ABI is independent of C++ ABI.

## 9. Suggested user folder structure

```
~/.config/repro/                     ← the config repo (git recommended)
├── repro.lua                         root: options + profiles
├── repro.lock.lua                    generated; provider decisions (committed)
├── modules/
│   ├── base.lua                      always-on: core user files, minimal env
│   ├── dev/
│   │   ├── git.lua
│   │   ├── ide.lua                    ← interface "dev/ide"
│   │   ├── ide/
│   │   │   └── vim.lua                ← provider "dev/ide/vim"
│   │   ├── c++.lua
│   │   └── rust.lua
│   └── sys/
│       ├── locale.lua                 ← system-scope content
│       └── udev-leds.lua
├── files/                            ← static files for src= (non-"./" refs)
│   └── gitconfig
├── plugins/
│   └── lua/                           ← pure-Lua backends/experiments
│       └── fake-packages.lua
└── (state lives OUTSIDE the repo: ~/.local/state/repro/repro.db)
```

Loading rules:
- module name = path under `modules/` minus extension; `modules/x/init.lua` ==
  `modules/x.lua` for interface/provider nesting (`dev/ide` + `dev/ide/vim`).
- `files{.src}` with `./`-prefix → relative to the module file (self-contained
  vendored modules); otherwise → relative to `files/`.
- `repro init` scaffold produces: `repro.lua` with a default profile, a starting
  `base.lua` module, `.gitignore` (state db is never in the repo).

## 10. Proposed internals plan (implementation view)

Two binaries of interest to Meson:
- `repro` — main CLI binary (eval, plan, apply, journal).
- `repro-lua-test` — dev-only runner for vendored `luaunit` suites (Lua API tests).

Core C++ components (suggest): `src/eval/` (LuaJIT host, bootstrap lua), `src/plan/`
(graph, resolver, hunks, hash), `src/apply/` (engine, journaling), `src/state/`
(SQLite schema + migrations), `src/plugins/` (discovery, C-ABI registrar),
`src/platform/` (fs/exec/user abstractions), `src/cli/`.

SQLite schema sketch:

```sql
meta(key TEXT PRIMARY KEY, value TEXT);           -- schema_version, repro_version
runs(id INTEGER PRIMARY KEY, started TEXT, mode TEXT, profile TEXT,
     exit_code INTEGER, counts TEXT /*json*/);
modules(run_id, name, decision_hash TEXT, status TEXT,
     PRIMARY KEY(run_id, name));
steps(run_id, module, step_id TEXT, type TEXT, status TEXT,
     detail TEXT /*json: digests, output tail*/, started TEXT, finished TEXT,
     error TEXT, PRIMARY KEY(run_id, module, step_id));
providers(run_id, interface TEXT, provider TEXT, source TEXT,
     decided_at TEXT, PRIMARY KEY(run_id, interface));
```

Milestones (proposal; each is Constitution-conformant: tests first):
- **M0** Skeleton: Meson, LuaJIT bootstrap, `rp` namespace scaffolding, `repro
  --version`, eval-only dry path with fixture repos.
- **M1** Eval + discovery: modules/interfaces/profiles/params, graph+cycle errors,
  `list`/`show`, `repro init`.
- **M2** Plan + dry-run: steps, decision hashes, provider resolution incl. ask
  + lockfile, `--dry-run` output.
- **M3** Apply + state: SQLite state, fake-backend-driven integration tests for
  files/env/first-package backend stub, journal, skip logic, `status`.
- **M4** Plugins: C ABI, pacman backend plugin, services backends (systemd,
  openrc), `plugins list`.
- **M5** System action types (users/locale/udev), `-s` mode hardening, docs.

## 11. Suggested bundled Lua libraries

All vendored as single-file pure-Lua sources under `src/lualib/vendor/` with
pins recorded for hermetic builds (no luarocks at runtime; no network at build):

| Library | Purpose | Why this one |
|---|---|---|
| **inspect.lua** (kikito) | Debug pretty-printing for `repro show` / error dumps | Tiny, stdlib-only, MIT |
| **serpent.lua** | Deterministic table serialization → feeds decision hashes and `repro.lock.lua` writing | Deterministic iteration; 1 file; MIT |
| **luaunit.lua** | Test framework for the embedded-Lua test binary `repro-lua-test` | 1 file, integrates with `meson test`; keeps tests in Lua, testing the actual Lua API |
| lpeg (via LuaJIT) | Available but NOT a dependency for v1 code; used opportunistically by users in their config | Already inside LuaJIT |
| cjson / luasocket / penlight | **Not bundled** | Host provides fs/exec/sqlite via C++; keeping the Lua surface minimal is a feature (YAGNI, Constitution simplicity gate) |

Real C dependency beyond LuaJIT: **sqlite3** (pinned subproject, WAL mode) — the
only heavy addition, justified by the user's state-DB choice.

## 12. Open questions (to carry into clarifications/spec)

| # | Question | Current lean |
|---|---|---|
| 1 | Parameter-value references (`rp.param_value`) exact syntax; string interpolation vs reference objects | Reference objects that substitute at plan time; keep hive simple |
| 2 | `providers` "none of the above" for an interface (skip provider entirely) | Defer to v2 unless user asks |
| 3 | Cross-mode consistency display (status reading both dbs, readable perms of /var/lib/repro state file) | Readable-but-not-writable system state file for `status` |
| 4 | Hook system for cross-module ordering beyond `requires` (e.g. "after udev") | YAGNI v1 |
| 5 | Env for shells that ignore environment.d | Ship `repro env-sh` docs page v1, real shell plugin later |

## 13. Acceptance / testability notes

Per Constitution IV, every numbered FR in §5 and §8 maps to:
- a **unit test** where logic is pure (resolver, hash, param substitution, lockio),
- a **fixture integration test** using sandboxed `$HOME` + fake pure-Lua backends
  for end-to-end `sync --dry-run` and `sync` (never the live system),
- a **docs check** for every new `rp.*` symbol (docs list is generated from the
  bootstrap's registry and diffed in CI).
