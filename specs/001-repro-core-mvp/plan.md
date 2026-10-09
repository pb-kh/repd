# Implementation Plan: repro — Declarative Environment Configuration Core

**Branch**: `001-repro-core-mvp` | **Date**: 2026-10-09 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/001-repro-core-mvp/spec.md`
(design context: [REQUIREMENTS.md](../../REQUIREMENTS.md))

## Summary

`repro` is a single C++23 binary embedding LuaJIT. Users keep a config
repository (`repro.lua`, `modules/**.lua`, `files/`, `plugins/lua/`,
generated `repro.lock.lua`). A run is a strict pipeline:
**discover → evaluate (sandboxed, recording-only Lua) → resolve (profiles,
dependency closure, interface providers) → plan (deterministic ordered steps,
each with a satisfied-check and a privilege) → preview or apply**, journaling
every step into a per-privilege-mode SQLite store. User and system passes are
separate explicit invocations. Package and service management live in
separately shipped backend add-ons (`.so` via a strings-only C ABI; first
add-ons `pacman` and `systemd`) or repo-local Lua backends; the core works
with zero add-ons for files/env/actions. Key technical decisions: `#embed` for
the Lua bootstrap (no code generator), in-house SHA-256 and canonical Lua
serializer for byte-stable fingerprints/lock/preview, always-run cheap checks
plus fingerprint-gated imperative actions (reconciles drift repair with
skip-on-unchanged), and sysroot/stub-based sandboxed integration tests.

## Technical Context

**Language/Version**: C++23 (GCC ≥ 15 / Clang ≥ 19); Lua 5.1 dialect on LuaJIT 2.1 (pinned commit)

**Primary Dependencies**: LuaJIT (embedded, static), SQLite 3.46 amalgamation, CLI11 2.4 (header-only); test-only: doctest 2.4, luaunit (vendored single file), Python 3 stdlib — all pinned Meson wraps / vendored (research R2, R3, R18, R19)

**Storage**: Per-mode SQLite (WAL): `~/.local/state/repro/repro.db` (user), `/var/lib/repro/repro.db` (system, 0600); repo-local `repro.lock.lua` (data-model §3)

**Testing**: `meson test` suites `unit` (doctest), `lua` (`repro-lua-test` + luaunit), `integration` (Python stdlib driving the real binary in temp-dir + `REPRO_SYSROOT` sandboxes, stub system tools, fake Lua backends; `-Dtest_hooks=true` builds)

**Target Platform**: Linux; Arch-derived distros primary (pacman/systemd add-ons); core must build & pass on NixOS (research R21)

**Project Type**: CLI tool (single binary) + C-ABI plugin add-ons

**Performance Goals**: up-to-date `sync -u` on a ~40-module repo < 2 s (SC-002; internal target < 1 s); plan for tens of modules dominated by one batched backend query per namespace

**Constraints**: hermetic offline build (`--wrap-mode=nodownload`); warnings-as-errors; byte-identical previews; no self-elevation; no mutation before the full plan succeeds; core binary < ~5 MB stripped; no runtime deps beyond libc/libdl

**Scale/Scope**: single machine per run; tens–low hundreds of modules; 8 step classes; 7 CLI commands; 2 backend namespaces

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| # | Principle / constraint | Pre-research | Post-design | Evidence |
|---|---|---|---|---|
| I | Declarative & reproducible; deterministic; idempotent; escape hatch marked | ✅ | ✅ | EVAL sandbox w/o `os`/`io`/`math.random` (R9); topo order + lexicographic tie-break, fixed class order (data-model §2); canonical serializer (R8); every step has a satisfied-check (R10); actions rendered `[imperative]` (cli.md) |
| II | Lua/LuaJIT only config surface; no rebuild to extend; documented API; C++ never parses config | ✅ | ✅ | All config semantics in embedded `src/lua/` bootstrap; C++ receives evaluated data trees; repo-local Lua backends; lua-api.md + docs registry test (R18); `repro.lock.lua` loaded by Lua sandbox, not C++ |
| III | C++23, Meson/Ninja only, no code generators, hermetic pinned deps, -Werror in CI | ✅ | ✅ (with tracked items) | `#embed` instead of generators (R4); wraps with `source_hash` + `nodownload` (R3); LuaJIT's internal buildvm step contained in subproject → Complexity Tracking |
| IV | Test-first, all in `meson test`, unit tests for parse/eval/apply, integration in sandbox never live | ✅ | ✅ | Suites unit/lua/integration; `REPRO_SYSROOT` + stubs + fake backends (R18); tasks phase must order tests before implementation |
| V | Preview for every apply; idempotent; resumable; structured audit log | ✅ | ✅ | `--dry-run` on the only mutating command (`sync`) (SC-005); per-step committed journal (data-model §3.1); stop-at-first-failure + re-check on resume (R15); adoption guard with backups (R14) |
| C1 | Single self-contained binary, no runtime interpreters/package managers | ✅ | ✅ (justified) | Core links LuaJIT/SQLite statically; add-on `.so` plugins are optional separately shipped deliverables, core runs with zero → Complexity Tracking |
| C2 | Config is plain Lua; no proprietary formats | ✅ | ✅ | Lock file is Lua (lockfile.md); SQLite is internal state, not configuration |
| W | Simplicity / YAGNI; added deps justified | ✅ | ✅ | Dropped serpent/inspect (R8), dropped in-repo `.so` loading (R17), deferred `--keep-going`, `rp.param_value`, `env-sh`, openrc; remaining deps listed below |

**Gate result: PASS** — no unjustified violations. Post-design re-check: PASS.

## Project Structure

### Documentation (this feature)

```text
specs/001-repro-core-mvp/
├── plan.md              # This file
├── research.md          # Phase 0: decisions R1–R21
├── data-model.md        # Phase 1: declaration/plan/state models, SQL schema
├── quickstart.md        # Phase 1: build/test + scenario walkthroughs
├── contracts/
│   ├── cli.md           # commands, flags, plan output format, exit codes, error catalogue
│   ├── lua-api.md       # rp.* surface, phases, sandbox, ctx, Lua backends
│   ├── plugin-abi.md    # include/repro/plugin.h, loading rules
│   └── lockfile.md      # repro.lock.lua format
├── checklists/requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
meson.build                     # project(cpp_std=c++23, warning_level=3), force_fallback_for
meson.options                   # test_hooks (bool, false), addons (feature)
.clang-format
include/repro/plugin.h          # installed C ABI header
src/
├── main.cpp
├── cli/                        # CLI11 setup, commands, plan/journal/status rendering
├── eval/                       # LuaJIT host, sandbox envs, phases, embedded.cpp (#embed), C++↔Lua data bridge
├── plan/                       # scope selection, graph+cycles, provider resolver, step builders, fingerprints
├── apply/                      # engine, step executors per class, post-steps, signal handling
├── state/                      # SQLite wrapper, migrations, journal/ledger repos, run.lock
├── plugins/                    # discovery, dlopen, ABI checks, backend facades (native + Lua)
├── platform/                   # Fs, Proc, Users, Paths (XDG, SUDO_USER, sysroot hook)
├── util/                       # sha256, errors (E_* codes + hints), strings
└── lua/                        # embedded bootstrap: repro/{init,builders,sandbox,canon,params,
                                #   resolve,merge,lock,backend}.lua, vendor/luaunit.lua (test binary only)
addons/
├── pacman/                     # shared_module pacman.so  (deliverable: repro-pacman)
└── systemd/                    # shared_module systemd.so (deliverable: repro-systemd)
tests/
├── unit/                       # doctest: sha256, plan graph, resolver, state, platform fakes
├── lua/                        # luaunit: canon, sandbox, params, merge, lock IO, docs registry
├── integration/                # Python unittest: per user story + SC tests; stubs/ (locale-gen, udevadm, useradd, usermod)
└── fixtures/
    ├── repos/                  # basic, providers, mixed, profiles, files-only, needs-pkgs, errors/*
    └── plugins/                # fake-packages.lua, fake-services.lua
docs/
└── lua-api.md                  # normative user docs (kept in sync by test)
subprojects/                    # luajit.wrap (+packagefiles overlay), sqlite3.wrap, cli11.wrap, doctest.wrap
```

**Structure Decision**: Single CLI project with component directories mirroring
the pipeline stages (eval → plan → apply → state), a platform layer as the only
syscall boundary, and first-party add-ons as separate `shared_module` targets
under `addons/` so they can be packaged independently. Tests are split by
suite so `meson test --suite` maps to the constitution's unit/integration
distinction.

### Build targets

| Target | Kind | Notes |
|---|---|---|
| `repro` | executable | core; static LuaJIT + SQLite |
| `repro-lua-test` | executable (not installed) | same host + luaunit; runs `tests/lua` |
| `test-*` | executables | doctest per component |
| `pacman`, `systemd` | shared_module | installed to `libdir/repro/plugins`; built when `-Daddons` enabled |

## Implementation phasing (input for /speckit-tasks)

Milestones follow user-story priority; each lands tests first.

1. **M0 Foundation** — Meson skeleton, wraps, `#embed` bootstrap loading, `repro --version`, platform layer + fakes, sha256, canon serializer, error type with hints. *(blocks all)*
2. **M1 US1** — discovery + sandboxed eval of modules/root config; files/env step classes; plan + preview; SQLite store, journal, managed_files; adoption guard; drift restore; `init`, `sync -u`, `status`, `journal`.
3. **M2 US2** — requires graph, cycles, interfaces/providers, resolver precedence, prompt, lock file, `--provider/--reselect/--yes`, `list providers`, `show`.
4. **M3 US3** — privilege classification, `-s` pass, root check, SUDO_USER resolution, system stores, users/locale/udev step classes with post-steps, other-mode pending counts.
5. **M4 US4** — profiles, params + `with_params`, per-profile overrides, `list profiles`.
6. **M5 US5** — backend facades, Lua backends, plugin ABI + loader, `plugins list`, packages/services step classes, missing-backend errors, `pacman`/`systemd` add-ons, actions (`ctx`).
7. **M6 Hardening** — SC tests (perf, determinism, resume/SIGINT, concurrency, error catalogue), docs/lua-api.md completion.

Note: packages/services step classes depend on M5 backends; US1's "declare a
package" acceptance test uses the fake Lua backend, so the backend facade and
Lua-backend loader move into M1 as a minimal slice (native `.so` loading stays in M5).

## Deviations from REQUIREMENTS.md (recorded for review)

| REQUIREMENTS item | Plan decision | Ref |
|---|---|---|
| §11 vendor serpent + inspect.lua | In-house `canon.lua` only | R8 |
| FR-B-3 `<repro>/plugins/*.so` | Repo-local extensions are Lua only | R17 |
| FR-M-3 field `modules` (sub-assemblies) | Not in v1; use `requires` | lua-api.md |
| FR-M-3 lacks system env | New field `system_env` | R12 |
| FR-A-3 single aggregated env file | One env file per module | R13 |
| FR-S-4 skip whole module on hash match | Cheap checks always run; only actions gated by fingerprint | R10 |
| FR-X-4 `--keep-going` | Out of this feature (v1.5) | R15 |
| `repro env-sh` | Docs page only | R13 |
| FR-B-6 `plugins list [--user|--system]` | `plugins list` reports both search sets; user dir only in user mode | plugin-abi.md |

## Complexity Tracking

| Item | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| LuaJIT subproject runs its own minilua/buildvm generation | LuaJIT cannot be built otherwise; constitution mandates LuaJIT pinned in-build | System LuaJIT is unpinned (Principle III); generation is confined to the third-party subproject, repro adds no generators |
| Optional `.so` add-ons outside the single binary | FR-020/021 require swappable package/service backends shipped separately | Compiling every backend into core couples core to every distro and violates "core knows nothing about specific package managers"; core still runs with zero add-ons |
| CLI11 dependency | ~7 subcommands, repeatable `--provider/--refresh/--reselect`, exact exit-code control | Hand-rolled parser is more code to test for no product value; header-only so no runtime footprint |
| doctest + luaunit + Python stdlib for tests | Three test layers mandated by Principle IV (C++ unit, Lua API, sandboxed E2E) | One layer cannot cover both C++ internals and the Lua contract; Python already present as Meson's runtime |
| `-Dtest_hooks` (`REPRO_SYSROOT`, `REPRO_TEST_ASSUME_ROOT`) | System-pass integration tests must never touch the live system | User-namespace sandboxes are unavailable in many CI runners; hooks are compiled out of release builds |
