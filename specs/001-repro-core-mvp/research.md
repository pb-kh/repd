# Phase 0 Research: repro core MVP

**Feature**: `001-repro-core-mvp` | **Date**: 2026-10-09
**Inputs**: [spec.md](./spec.md), [REQUIREMENTS.md](../../REQUIREMENTS.md), Constitution v1.0.0

Each entry: **Decision** / **Rationale** / **Alternatives considered**. Where a
decision deviates from REQUIREMENTS.md, the deviation is called out explicitly.

---

## R1. Toolchain baseline

- **Decision**: C++23, minimum **GCC 15** or **Clang 19**; Meson ≥ 1.4 with
  Ninja. CI matrix: latest GCC + latest Clang on Arch (dev box today: GCC 16.2,
  Clang 22.1, Meson 1.12). Use `std::expected`, `std::print`/`std::format`,
  `std::ranges`, `std::span`; no `std::modules` (tooling still uneven under Meson).
- **Rationale**: GCC 15 / Clang 19 are the first releases with `#embed` usable
  from C++ (see R4) and complete `std::expected`/`std::print`. Arch-derived
  distros (primary target) ship newer.
- **Alternatives**: C++23 modules (rejected: Meson/compiler module scanning still
  fragile; no payoff at this size); GCC 13 baseline (rejected: no `#embed`,
  forcing a code generator).

## R2. LuaJIT pinning and build

- **Decision**: LuaJIT **v2.1 rolling branch, pinned to an exact commit** in
  `subprojects/luajit.wrap` (`revision = <sha>`, `source_hash` on the tarball),
  built by a project-maintained overlay `subprojects/packagefiles/luajit/meson.build`
  (minilua → buildvm → `lj_vm.o` + static `libluajit`). Root `meson.build` sets
  `default_options: ['force_fallback_for=luajit,sqlite3']` so a system LuaJIT is
  never silently used. Built with `LUAJIT_ENABLE_LUA52COMPAT` **off** (plain 5.1
  semantics, matching the constitution).
- **Rationale**: LuaJIT has no tagged releases since 2.1 went rolling; a commit
  pin is the only reproducible pin. The minilua/buildvm generation steps are
  LuaJIT's own internal build, contained inside the subproject — not a code
  generator in repro's code (recorded in Complexity Tracking).
- **Alternatives**: system `luajit` via pkg-config (rejected: unpinned, violates
  hermetic builds); PUC Lua 5.4 (rejected: constitution mandates LuaJIT);
  WrapDB luajit port (rejected: lags upstream and pins an old beta).

## R3. Hermetic dependency fetching

- **Decision**: All wraps carry `source_hash`. `subprojects/packagecache/` is
  populated once with `meson subprojects download` (developer action, network)
  and **CI and release builds configure with `--wrap-mode=nodownload`**, so no
  build step touches the network. Documented in quickstart.
- **Rationale**: Satisfies Principle III ("no build step depends on network")
  without committing third-party tarballs to git.
- **Alternatives**: vendoring sources into the tree (rejected: repo bloat, harder
  updates); git submodules (rejected: second dependency mechanism next to Meson).

## R4. Embedding the Lua bootstrap into the binary

- **Decision**: Lua sources under `src/lua/` are embedded with C/C++ **`#embed`**
  in `src/eval/embedded.cpp`, one array per file, registered in a static
  `{module_name, bytes}` table consumed by a custom `package.searchers` entry.
  The pedantic "extension" diagnostic is locally suppressed with
  `#pragma GCC diagnostic` around the includes. Meson `depend_files` lists every
  embedded `.lua` so edits trigger rebuilds.
- **Rationale**: No code generator (Principle III), source stays as real `.lua`
  files (lintable, testable by `repro-lua-test`), single self-contained binary.
- **Alternatives**: `custom_target` + xxd/script (rejected: code generator);
  raw string literals in `.cpp` (rejected: unlintable, escapes); `.incbin` asm
  (works but toolchain-specific and opaque); LuaJIT `-b` bytecode objects
  (rejected: requires running host luajit in build — another generation step).

## R5. State database

- **Decision**: **SQLite 3.46.x** via WrapDB wrap (amalgamation), pinned,
  compiled with `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_DEFAULT_FOREIGN_KEYS=1`,
  `SQLITE_THREADSAFE=0`. Opened in **WAL** mode, `synchronous=NORMAL`. One DB per
  mode: user `${XDG_STATE_HOME:-~/.local/state}/repro/repro.db`; system
  `/var/lib/repro/repro.db` (dir 0700, file 0600, root-owned). Schema in
  [data-model.md](./data-model.md) §State; migrations are numbered SQL strings
  in `src/state/migrations.cpp`, applied in a transaction keyed by `meta.schema_version`.
- **Rationale**: User decision (REQUIREMENTS §1). Transactions give crash-safe
  per-step journaling (Principle V); one file per mode gives the independent
  progress required by FR-015.
- **Alternatives**: append-only JSONL journal (rejected: user chose SQLite; status
  queries awkward); one shared DB (rejected: privilege crossing, FR-015).

## R6. Same-mode concurrency guard

- **Decision**: `flock(LOCK_EX|LOCK_NB)` on `<state-dir>/run.lock` taken before
  plan; failure → exit 2 with "another repro run is active in <mode> mode
  (pid N)" (pid written into the lock file for the message only). Read-only
  commands (`status`, `journal`, `list`, `show`, `sync --dry-run`) take **no**
  lock: WAL readers see a consistent snapshot, and a dry-run writes nothing.
- **Rationale**: SC-009 one-proceeds-one-refuses; lock is per state dir so the
  two modes never contend. Named `run.lock` to avoid confusion with
  `repro.lock.lua`.
- **Alternatives**: SQLite `BEGIN EXCLUSIVE` held for the whole run (rejected:
  blocks journal readers, long-held write txn defeats per-step commits).

## R7. Hashing

- **Decision**: In-tree **SHA-256** (`src/util/sha256.{hpp,cpp}`, ~150 lines,
  public-domain-derived), verified against NIST FIPS 180-4 test vectors.
  Exposed to Lua (bootstrap only) as `rp._internal.sha256`.
- **Rationale**: Only hashing need is fingerprints and file digests; pulling in
  OpenSSL/libsodium would add a runtime/system dependency against the
  single-binary constraint.
- **Alternatives**: OpenSSL `libcrypto` (rejected: external runtime dep); BLAKE3
  vendored (rejected: no benefit at these sizes, SHA-256 is the documented choice).

## R8. Canonical serialization (fingerprints, lock file, `show`)

- **Decision**: One in-house Lua module `src/lua/repro/canon.lua` providing
  `canon.encode(v)` (compact, byte-stable: keys sorted by type then value,
  integers vs floats normalized, strings `%q`-quoted, rejects functions /
  userdata / cdata / cycles) and `canon.pretty(v)` (indented, same ordering).
  Used for decision hashes, writing `repro.lock.lua`, and `repro show`.
  **Deviation from REQUIREMENTS §11**: serpent and inspect.lua are **not**
  vendored; only luaunit is.
- **Rationale**: Fingerprint stability is a correctness property (SC-003, FR-011);
  owning ~200 tested lines is safer than relying on a general serializer's
  ordering options. One serializer for all three uses = one set of tests.
- **Alternatives**: serpent with `sortkeys` (rejected: ordering of mixed key
  types and number formatting not contractually stable); inspect.lua for `show`
  (redundant with `canon.pretty`).

## R9. Evaluation sandbox and the declarative contract (FR-018)

- **Decision**: Each module file and `repro.lua` is loaded with `loadfile`
  into a **fresh environment table** (`setfenv`) containing only: `rp` (eval
  facade), `string`, `table`, `math` (minus `random`/`randomseed`), `ipairs`,
  `pairs`, `next`, `select`, `type`, `tostring`, `tonumber`, `error`, `assert`,
  `pcall`, `unpack`, `setmetatable`/`getmetatable`. **Not present**: `io`, `os`,
  `require`, `load*`, `dofile`, `debug`, `ffi`, `jit`, `package`, `collectgarbage`.
  The process is in phase `EVAL`; every mutating facade (`rp.packages.install`,
  `rp.fs.write`, …) checks the phase and raises
  `E_DECLARATIVE: <fn> performs a change while configuration is being read; declare it in the module's <field> instead`.
  Read-only queries allowed in eval: `rp.fs.{exists,read,stat,list}`,
  `rp.sys.{hostname,user,home,os_release,arch}`, `rp.packages.installed(names)`,
  `rp.services.state(unit)`. Unknown global access in module env triggers a
  `__index` hook that errors with a "did you mean rp.X?" hint (FR-E-6).
- **Rationale**: `pairs` ordering is non-deterministic in LuaJIT, but it is only
  dangerous where it leaks into output order; the bootstrap never iterates user
  tables with `pairs` without sorting (enforced by a lint test that greps the
  bootstrap for raw `pairs(` outside `canon.sorted_pairs`). Removing
  `os.time`/`math.random` removes the most common ambient-state leaks
  (Principle I). Read-only queries are deliberately allowed and their results
  are captured in the resolved declaration that is hashed (R10).
- **Alternatives**: separate Lua state per module (rejected: slower, no benefit
  given fresh env tables); full static analysis of config (rejected: violates
  "C++ never parses config" and is undecidable anyway).

## R10. Module fingerprint (decision hash) and skip semantics — reconciling FR-010/FR-011 with drift repair

- **Decision**: fingerprint = SHA-256 of `canon.encode` of:
  `{ v = FP_SCHEMA, repro = REPRO_VERSION, api = LUA_API_VERSION, mode, module = name,
  source = sha256(module file bytes), decl = resolved declaration (post-params,
  post-with_params, functions replaced by their source-position ids),
  src_digests = { [path] = sha256 } for every files/udev src,
  params = resolved params, providers = resolved map for its requires,
  backends = { [ns] = name .. "@" .. version } for namespaces it uses }`.
  **Skip semantics** (resolves an apparent conflict between US1-AS5 "drift is
  restored" and FR-011 "skip unchanged modules"):
  1. Steps of *local* step classes (files, env, udev, locale, users) and of
     *backend* classes (packages, services) **always run their satisfied-check**
     — these are cheap (stat + digest; one batched `installed()` query per
     backend; one batched service query). Unsatisfied ⇒ planned, regardless of
     fingerprint. This is what restores drift and protects against deleted
     records (Edge case: "never skip on bookkeeping alone").
  2. **Action steps** (imperative escape hatch, potentially slow/networked
     checks) are skipped without running `check` when the module's fingerprint
     equals the last recorded *complete* fingerprint for this mode. Otherwise
     their `check` runs.
  3. `--refresh MODULE` disables (2) for that module (and forces re-apply of its
     non-satisfied-check steps' post-steps, e.g. udev reload).
  4. Module status: `applied` (fp matches, all steps ok), `stale` (fp differs),
     `pending` (never applied / unsatisfied steps), `failed` (last run failed).
- **Rationale**: Keeps the idempotence guarantee honest (checks are the source
  of truth) while still using the fingerprint for what it is good at — status,
  avoiding expensive imperative checks, and audit. Up-to-date runs stay well
  under the 2 s target (SC-002): tens of stats + one `pacman -Qq` + one
  `systemctl is-enabled u1 u2 …`.
- **Alternatives**: skip whole module on fingerprint match (rejected: breaks
  US1-AS5 drift restore and the "never trust bookkeeping alone" edge case);
  never use fingerprints (rejected: FR-011, actions would re-check every run).

## R11. Parameter-driven content (REQUIREMENTS open question 1)

- **Decision**: v1 supports **`with_params = function(p, sys) return {…} end`**
  only. It runs in the EVAL phase (same sandbox, recording-only) after params are
  resolved; the returned fragment is merged into the module: list fields
  concatenated (then de-duplicated preserving first occurrence), map fields
  merged with a plan-time error on conflicting keys. `rp.param_value` references
  are **deferred** (not in v1).
- **Rationale**: One mechanism, plain Lua, fully covered by the sandbox and the
  fingerprint (the resolved declaration is hashed). Reference objects would add a
  second substitution model to document and test.
- **Alternatives**: value-reference objects (deferred; can be added
  backward-compatibly); string interpolation (rejected: stringly typed).

## R12. Privilege classification of steps

- **Decision** (deterministic, at plan time):

  | Class | Privilege |
  |---|---|
  | files | `user` if resolved `dest` is under the target user's home, else `system`; explicit `scope` may only *confirm* or set `user` for a non-home path; `scope="system"` under home is a plan error (would create root-owned files in `$HOME`). |
  | env | `env` → user (`~/.config/environment.d/`); `system_env` → system (`/etc/environment.d/`). |
  | packages | as declared by the selected backend (`pacman` → system). |
  | services | `scope` field: `system` (default) or `user`. |
  | users, locale, udev | system. |
  | actions | the action's required `mode` field. |

  **Target user**: the invoking user; under `sudo`, `$SUDO_USER` (via
  `getpwnam`) for home resolution and config discovery (FR-C-2). Config repo
  default: `${XDG_CONFIG_HOME:-~/.config}/repro`, overridable by `--config` /
  `REPRO_CONFIG`.
- **New field `system_env`** is added to the module vocabulary (REQUIREMENTS
  FR-M-3 listed only `env`, which left system scope unexpressible).
- **Rationale**: Privilege must be computable without executing anything, so the
  user pass can report "N system changes pending" (US3-AS1).

## R13. Shared-file step classes (env, locale) under partial application

- **Decision**:
  - **env**: one file **per module**: `90-repro-<slug>.conf` where `slug` =
    module name with `/` → `--` (e.g. `90-repro-dev--c++.conf`). The same
    variable set by two modules in the selected closure is a plan-time conflict
    error naming both modules. A `repro env-sh` command is **out of scope for
    this feature** (doc page only, per REQUIREMENTS open question 5).
  - **locale**: managed block between `# BEGIN repro` / `# END repro` markers in
    `/etc/locale.gen` (union of all selected modules' locales, sorted), then
    `locale-gen`; `/etc/locale.conf` `LANG=` written only if a module declares
    `default` (two different defaults → plan error).
  - **udev**: one file per rule in `/etc/udev/rules.d/`; a single deduplicated
    `udevadm control --reload` post-step at the end of the system pass if any
    udev step changed.
- **Rationale**: Per-module files make partial application safe (applying
  profile A never erases profile B's variables). Managed blocks are the minimal
  intrusion into distro-owned files.

- **Revision (2026-10-09)**: environment.d is read only by systemd, so env is
  now written per *target* (`sh` → `/etc/profile.d` + a managed block in the
  login profile, `fish` → `conf.d`, `environment.d`), chosen by
  `rp.option("env.targets")` (default `auto`: login shell + systemd
  detection). Targets are built into core, not plugins: they are file formats,
  not external engines. See docs/lua-api.md "Environment variables".

## R14. Adoption guard and file ownership

- **Decision**: Each mode's DB has a `managed_files` table (path, kind, digest or
  link target, module). A destination is *ours* iff a row exists and the on-disk
  state matches the recorded digest/target (or is absent). Existing + not ours +
  different from desired ⇒ step `blocked`, run stops before any mutation of that
  module (default) with message naming the path and `--adopt` / `adopt = true`.
  Existing + identical to desired ⇒ silently recorded as managed (no mutation).
  Adoption keeps a backup `<dest>.repro-adopted-<UTC timestamp>` and is journaled.
  Drift on a *managed* file (ours, but on-disk digest ≠ recorded) is restored
  without prompting (US1-AS5).
- **Rationale**: Principle V safety + FR-016; backups make adoption recoverable.
- **Alternatives**: always overwrite with backup (rejected: spec requires refusal).

## R15. Plan-then-apply, failure policy, interruption

- **Decision**: Pipeline is strictly `discover → eval → resolve → plan (with
  checks) → [preview | apply]`. No mutation before the whole plan for the
  selected scope succeeded (NFR-3). Apply re-runs each step's check immediately
  before mutating (TOCTOU guard). Stop at first failure (exit 1), journal
  committed per step. `SIGINT`/`SIGTERM` set a flag checked between steps and
  forwarded to the running child process; the run row is closed with exit 130.
  `--keep-going` is **not in this feature** (REQUIREMENTS marks it v1.5).
- **Rationale**: SC-004 resumability: completed steps are committed rows; re-run
  re-checks everything.

## R16. Provider resolution & lock file

- **Decision**: Precedence exactly as FR-006. Prompt is shown on the controlling
  TTY (`/dev/tty`) only if stdin and stderr are TTYs and `--yes` is absent;
  otherwise ambiguity ⇒ exit 2 with candidates listed. `repro.lock.lua` is a Lua
  file `return { version = 1, providers = { ["dev/ide"] = { provider = …,
  source = …, decided_at = … } } }`, written atomically (tmp + `rename`) after
  a successful *plan* of a non-dry-run sync, emitted with `canon.pretty` (stable
  diffs). Under `sudo` the lock is written with the config repo owner's uid/gid.
  Format in [contracts/lockfile.md](./contracts/lockfile.md).
- **Rationale**: FR-007; deterministic text keeps git diffs minimal.

## R17. Backend plugins (`.so`) and pure-Lua backends

- **Decision**: C ABI header `include/repro/plugin.h`, `REPRO_PLUGIN_ABI = 1`,
  entry `repro_plugin_init(abi, host, host_api, registrar, registrar_api)` (host services and registration passed as function tables, so the executable exports no symbols).
  Data crosses the boundary as NUL-terminated UTF-8 string arrays only (no
  structs with layout risk beyond the vtable). Loaded with
  `dlopen(RTLD_NOW|RTLD_LOCAL)`, fail-soft per plugin. Search path order:
  `/usr/lib/repro/plugins`, `/usr/lib64/repro/plugins`,
  `~/.local/lib/repro/plugins` (user mode only), then `<config>/plugins/lua/*.lua`.
  **In-repo `.so` plugins (`<config>/plugins/*.so`) are dropped** from v1 —
  repo-local extensions are Lua only (reduced trust, FR-022); loading native
  code from a git-cloned repo, especially in the root pass, is an unnecessary
  risk. First add-ons built in this repo as separate Meson `shared_module`s:
  `addons/pacman` (packages, system privilege) and `addons/systemd` (services).
  *(Revised: `addons/openrc` (services) is in the MVP — non-systemd hosts are a
  first-class target; systemd and openrc probes defer to the init that booted.
  `addons/yay` (packages, user privilege, AUR) runs yay on the terminal through
  the host's `exec_tty` so sudo can prompt for a password.
  Packages are routed per entry, not by one selected backend: `"backend:name"`,
  or the default from `rp.option("packages.default_backend")`, which has no `auto`.
  One packages step per (module, backend), id `<module>/packages/<backend>`.
  Installs no longer refresh first (`-Sy` + `-S` is a partial upgrade);
  `repro sync --upgrade` plans an `upgrade` step per backend via the optional
  `repro_packages_vt.upgrade`.
  `addons/flatpak` registers two packages backends from one plugin, `flatpak`
  (system installation, system privilege) and `flatpak-user` (`--user`, user
  privilege): privilege is fixed at registration and decides the pass, so the
  installation is part of the backend name rather than an option.)*
  Auto-selection: each backend exports `probe()`; for
  `auto`, all probing-positive backends for a namespace are ranked by
  `(origin: system > user-dir > repo-lua, then name)`; first wins and is
  printed in the preview.
  Missing backend message names the add-on package: the core keeps a small
  static hint table `{packages = {pacman = "repro-pacman"}, services = {systemd = "repro-systemd"}}`
  keyed by detected platform tool, e.g. "install it with `pacman -S repro-pacman`".
- **Rationale**: FR-020..022, US5. Strings-only ABI keeps the plugin ABI
  independent of the C++ ABI.

- **Revision (2026-10-09)**: plugins are named `librepro-<name>.so` and live
  directly in one system directory set at build time (`-Dplugin_dir`, macro
  `REPRO_PLUGIN_DIR`, default `/usr/lib/repro`) plus `~/.local/lib/repro` in
  user mode.

## R18. Testing strategy (Principle IV)

- **Decision**:
  - **C++ unit tests**: **doctest** 2.4.x (WrapDB, header-only), one test
    executable per component (`test-plan`, `test-state`, …) registered via
    `test()` with suites `unit`.
  - **Lua unit tests**: `repro-lua-test` (dev-only binary embedding the same host
    + vendored **luaunit**) runs `tests/lua/test_*.lua` (canon, resolver,
    sandbox, params). Suite `lua`.
  - **Integration tests**: Python 3 **stdlib-only** (`unittest`, available
    wherever Meson runs) under `tests/integration/`, invoking the real `repro`
    binary against fixture config repos in a temp dir with: `HOME`,
    `XDG_*` pointed into the temp dir; **`REPRO_SYSROOT`** prefixing every
    system path (`/etc`, `/var/lib/repro`, …); **`REPRO_TEST_ASSUME_ROOT=1`**
    replacing the euid check; stub `locale-gen`/`udevadm`/`useradd`/`usermod`
    scripts first on `PATH` that log invocations; fake backends as pure-Lua
    plugins. These env hooks are compiled in **only** when Meson option
    `-Dtest_hooks=true` (default `false`; dev/CI builds enable it); release
    builds ignore them. Suite `integration`. **Never** touches the live system.
  - **Error-message catalogue** (SC-007): `tests/integration/test_errors.py`
    iterates a table of fixtures → expected exit code + "names element" +
    "suggests action" regexes.
  - **Docs check**: a Lua test enumerates the bootstrap's `rp` API registry and
    asserts each symbol has a heading in `docs/lua-api.md`.
  - **Determinism**: SC-003 test runs `sync --dry-run` twice (and once with
    `LUAJIT` hash-seed perturbation via different table insertion order
    fixtures) and byte-compares.
- **Rationale**: Python stdlib is already a hard dependency of Meson, so it adds
  nothing to the toolchain; temp-dir + sysroot sandboxes work in unprivileged
  CI without user namespaces.
- **Alternatives**: bubblewrap/unshare sandboxes (rejected: unprivileged userns
  often disabled in CI); Catch2 (heavier compile times than doctest for no gain);
  shell-script integration tests (fragile assertions).

## R19. CLI parsing and output

- **Decision**: **CLI11** 2.4.x (WrapDB, header-only) for subcommands/flags.
  Output via `std::print` to stdout (plan/results) and stderr (diagnostics);
  colors only when `isatty(1)` and `NO_COLOR` unset. Exit codes 0/1/2/130 (FR-023).
  Preview text format is a contract ([contracts/cli.md](./contracts/cli.md)) and
  contains no timestamps, PIDs or absolute temp paths (beyond declared dests) so
  it is byte-identical across runs.
- **Rationale**: ~15 flags across 7 subcommands with repeatable options; CLI11
  avoids a hand-rolled parser bug surface, header-only so the single-binary
  property holds.
- **Alternatives**: hand-written parser (viable; rejected for effort/bug-surface),
  getopt_long (no subcommands).

## R20. Platform abstraction (NFR-4)

- **Decision**: `src/platform/` exposes `Fs` (stat, read, atomic write via
  tmp+fsync+rename, symlink, mkdir -p, chmod/chown), `Proc` (spawn with argv
  vector — **never via a shell** except `ctx.exec(string)` in actions, which is
  explicitly `/bin/sh -c` and documented), `Users` (getpwnam, groups), `Paths`
  (XDG, sysroot prefixing). All other code goes through it; unit tests use an
  in-memory `Fs` fake.
- **Rationale**: Constitution "platform-specific code isolated"; also the single
  choke point for `REPRO_SYSROOT`.

## R21. Platform scope note (constitution vs spec)

- **Finding**: Constitution names NixOS the "primary reference platform"; the
  spec and REQUIREMENTS make Arch-derived distros the primary dev target with
  pacman as the first backend, and in-place `/etc` mutation is not viable on
  NixOS. **Resolution for this feature**: the core (files/env/user mode, Lua
  backends) must build and pass tests on NixOS; system-pass backends for NixOS
  are post-v1. No constitutional violation (the constitution requires
  platform isolation, which R20 provides), but the wording should be revisited
  in a future constitution PATCH.

---

## Resolved unknowns

All Technical Context items are resolved; no `NEEDS CLARIFICATION` remains.
REQUIREMENTS open questions: #1 → R11; #2 deferred (v2); #3 → system DB is
root-only 0600, `repro status -u` shows system mode as "unavailable (run with
sudo)"; #4 deferred (YAGNI); #5 → R13 (doc only).
