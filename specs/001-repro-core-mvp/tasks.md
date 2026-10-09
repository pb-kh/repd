# Tasks: repro — Declarative Environment Configuration Core

**Feature**: `001-repro-core-mvp` | **Date**: 2026-10-09
**Status**: Implementation phasing M0–M6 | **Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

**Test Strategy**: Test-first (Constitution IV); suites: `unit` (C++ doctest), `lua` (luaunit), `integration` (Python sandboxed). Tests written before implementation; each user story must pass its independent test scenarios from quickstart.md.

---

## Phase 1: Setup (M0 Foundation — Blocks All Other Work)

**Purpose**: Meson project skeleton, build infrastructure, and fundamental utilities that all subsequent work depends on.

**Gate**: All Phase 1 tasks MUST complete before any other work begins.

### Meson & Build Configuration

- [X] T001 Create root meson.build with C++23, warning_level=3, default_options: `['force_fallback_for=luajit,sqlite3', 'cpp_std=c++23']`, subprojects, and main `repro` executable target in src/
- [X] T002 [P] Create meson.options with `test_hooks` (bool, default false), `addons` (feature) options
- [X] T003 [P] Create .clang-format configuration with Google style base, column limit 100
- [X] T004 Create subprojects/luajit.wrap with exact commit pin and source_hash (research R2)
- [X] T005 Create subprojects/sqlite3.wrap with 3.46 amalgamation source_hash
- [X] T006 Create subprojects/cli11.wrap with 2.4 header-only
- [X] T007 Create subprojects/doctest.wrap with 2.4 header-only
- [X] T008 [P] Create subprojects/packagefiles/luajit/meson.build for LuaJIT build (minilua → buildvm, research R2)

### Core C++ Utilities (Non-testable utilities first)

- [X] T009 [P] Implement src/util/sha256.hpp and src/util/sha256.cpp with FIPS 180-4 test vectors in tests/unit/test_sha256.cpp (research R7)
- [X] T010 [P] Create src/util/errors.hpp with error types (`E_*` codes, hints), std::expected<T, Error> wrapper — *errors are a struct + `std::expected` alias in errors.hpp; formatting in errors.cpp*
- [X] T011 [P] Create src/util/strings.hpp with string utilities (trim, split, replace, canonicalize)
- [X] T012 Implement doctest unit test infrastructure: create tests/unit/ directory and meson.build linking doctest, compile test-* per component, register with suite `unit`

### Platform Abstraction Layer

- [X] T013 [P] Implement src/platform/fs.hpp with Fs interface (stat, read, atomic write via tmp+fsync+rename, symlink, mkdir -p, chmod, chown)
- [X] T014 [P] Implement src/platform/fs.cpp for Linux (real FS + REPRO_SYSROOT hook for tests, research R20)
- [X] T015 [P] Implement src/platform/proc.hpp and src/platform/proc.cpp with Proc (spawn argv, no shell except in actions)
- [X] T016 [P] Implement src/platform/users.hpp and src/platform/users.cpp with Users (getpwnam, groups, SUDO_USER resolution per research R12)
- [X] T017 [P] Implement src/platform/paths.hpp and src/platform/paths.cpp with Paths (XDG, sysroot hook, config dir resolution per research R12)
- [X] T018 Create tests/unit/test_platform.cpp with in-memory Fs fake for testing other components — *fake lives in tests/unit/mem_fs.hpp; platform tests in test_platform.cpp*

### Lua Bootstrap & Embedding

- [X] T019 Create src/lua/repro/init.lua entry point that registers the `rp` global facade (phase detection, recording-only eval, sandbox setup)
- [X] T020 [P] Create src/lua/repro/canon.lua (research R8) with `canon.encode(v)` (byte-stable serialization: sorted keys, int/float normalization, %q strings, function rejection, cycle detection) and `canon.pretty(v)` (indented version)
- [X] T021 [P] Create src/lua/repro/sandbox.lua with fresh environment tables per module (research R9): include rp, string, table, math (no random), select, pairs, ipairs, next, type, tostring, tonumber, error, assert, unpack, setmetatable, getmetatable; exclude io, os, require, debug, ffi, jit, package
- [X] T022 Create src/lua/repro/params.lua with parameter resolution (type validation, one_of constraints, defaults)
- [X] T023 Create src/lua/repro/resolve.lua with provider resolution state machine (cli → profile → pin → lock → only-option → prompt → E_AMBIGUOUS, research R16) — *deviation: provider resolution is implemented once, in C++ (src/plan/resolver.cpp, T065), not duplicated in Lua*
- [X] T024 Create src/lua/repro/merge.lua with `with_params` fragment merging (list concat + dedup, map merge, conflict error, research R11)
- [X] T025 Create src/lua/repro/lock.lua with `repro.lock.lua` file I/O (Lua table serialize/deserialize)
- [X] T026 [P] Create src/lua/repro/backend.lua facade with namespace dispatch (packages, services), call routing to native/.so or repo-local Lua backends — *Lua facade routes to the C++ BackendRegistry, which wraps native and Lua backends alike*
- [X] T027 Create src/lua/repro/builders.lua with module/interface/param/profile builder functions (`rp.module`, `rp.interface`, `rp.profile`, `rp.param_*)
- [X] T028 Implement src/eval/embedded.cpp with C++ `#embed` of all src/lua/repro/*.lua files into static table keyed by module name, registered in custom `package.searchers` entry
- [X] T029 [P] Create tests/lua/test_canon.lua with luaunit tests for byte-stable encoding (sorted keys, int/float, cycles, functions)
- [X] T030 [P] Create tests/lua/test_sandbox.lua with luaunit tests for eval sandbox restrictions (os/io/require/debug/ffi forbidden, read-only queries allowed)
- [X] T031 [P] Create tests/lua/test_params.lua with luaunit tests for parameter resolution and validation

### CLI Skeleton

- [X] T032 Implement src/main.cpp with minimal CLI11 setup (subcommands: init, sync, status, journal, list, show, plugins; --help, --version)
- [X] T033 Create src/cli/commands.hpp and src/cli/commands.cpp skeleton with command registration
- [X] T034 Implement `repro --version` command returning `REPRO_VERSION` and `REPRO_API_VERSION`

**Checkpoint**: Phase 1 complete. Build succeeds with `meson test --suite unit` and `meson test --suite lua` passing. All C++ and Lua modules are in place. Platform layer is testable.

---

## Phase 2: Foundational (M0–M1 Early — Blocking All User Stories)

**Purpose**: Core data structures, SQLite state management, Lua host setup, and pipeline infrastructure that every user story depends on. Must complete before US1 work begins.

**Gate**: Phase 2 completion unblocks all user story phases.

### C++ Data Model & Declaration Tree

- [X] T035 [P] Create include/repro/config.hpp and src/state/config.cpp with ConfigRepository struct (root, entry, lock, modules_dir, files_dir, lua_plugins_dir paths per research R12)
- [X] T036 [P] Create src/eval/declaration.hpp with Lua-evaluated data model structs: Module, Interface, Profile, Param, FileEntry, ServiceEntry, UserEntry, LocaleEntry, UdevEntry, Action, Backend (matching data-model.md §1) — *also src/eval/declaration.cpp (Value → struct conversion)*
- [X] T037 [P] Create src/plan/scope.hpp with ModuleName normalization (`^[a-z0-9_+-]+(/[a-z0-9_+-]+)*$`) and validation helpers

### SQLite State Management (R5)

- [X] T038 Create src/state/migrations.cpp with numbered SQL schema migrations applied in transaction (meta, runs, run_providers, run_modules, steps, module_state, managed_files, managed_packages per data-model.md §3.1)
- [X] T039 [P] Create src/state/db.hpp and src/state/db.cpp with SQLite wrapper: open per-mode (user `~/.local/state/repro/repro.db`, system `/var/lib/repro/repro.db` 0600), WAL mode, `synchronous=NORMAL`, `SQLITE_DEFAULT_FOREIGN_KEYS=1`, migrate on open
- [X] T040 [P] Create src/state/ledger.hpp and src/state/ledger.cpp with repository interfaces: InsertRun, AppendStep, UpdateModuleState, QueryManagedFile, InsertManaged{Files,Packages}
- [X] T041 Create src/state/run_lock.hpp and src/state/run_lock.cpp with `run.lock` exclusive lock guard (`flock(LOCK_EX|LOCK_NB)` beside state dir, error message includes pid, research R6)
- [X] T042 Create tests/unit/test_state.cpp with doctest for DB creation, migration, ledger inserts/queries, lock guard behavior

### Lua Host & Evaluation Pipeline

- [X] T043 [P] Create src/eval/host.hpp and src/eval/host.cpp with LuaJIT host setup: `lua_State*` creation, bootstrap loading via `#embed`, phase tracking enum (EVAL, PLAN, APPLY)
- [X] T044 Create src/eval/discovery.hpp and src/eval/discovery.cpp: scan `modules/`, `modules/*/init.lua`, validate names, detect `interface` vs `module`, check name/file collisions (research R2, data-model.md §Declaration)
- [X] T045 Create src/eval/evaluate.cpp with module evaluation pipeline: for each discovered module, set phase=EVAL, load into fresh sandbox (research R9), run `with_params` fragment merge (research R11), record resolved declaration, return Lua table for C++
- [X] T046 Implement C++↔Lua data bridge in src/eval/bridge.cpp: convert Lua tables to C++ declaration structs, handle type coercion, validate schema (research R9)
- [X] T047 Create tests/unit/test_eval.cpp with doctest for discovery, declaration validation, typo errors

### Plan Model & Fingerprinting

- [X] T048 [P] Create src/plan/plan.hpp with Plan struct: mode, profile, requested, selected, providers, backends, steps (ordered list, per data-model.md §2)
- [X] T049 [P] Create src/plan/step.hpp with Step struct: id (deterministic `<module>/<class>/<key>`), module, class (enum: packages, files, env, services, users, locale, udev, action), privilege, description, desired, check_result, diff_summary, imperative
- [X] T050 [P] Create src/plan/fingerprint.hpp and src/plan/fingerprint.cpp implementing decision hash per research R10: SHA-256 of canon-encoded map including schema version, repro version, API version, mode, module, source sha256, resolved declaration, params, providers, backends
- [X] T051 [P] Create src/plan/graph.hpp and src/plan/graph.cpp with scope selection (research R10): closure over requires, interface→provider resolution, cycle detection (DFS), topological sort with lexicographic tie-break
- [X] T052 Create tests/unit/test_plan.cpp with doctest for fingerprinting (RC-003 byte-stability), graph cycles, topo ordering

### Error Catalog & Reporting

- [X] T053 Create src/util/errors.cpp implementing error codes from contracts/cli.md: E_CONFIG, E_UNKNOWN (with closest-match suggestions), E_CYCLE, E_AMBIGUOUS, E_PROVIDER, E_NO_PROVIDER, E_PARAM, E_BLOCKED, E_PRIVILEGE, E_BUSY, E_DECLARATIVE, etc.
- [X] T054 Implement `std::print`-based error output to stderr with structured format: error code, offending element, suggested action (FR-024, SC-007)
- [X] T055 Create tests/integration/test_errors.py with error catalogue: fixture → expected exit code + "element names" + "suggests action" regexes (SC-007)

### Integration Test Framework

- [X] T056 Create tests/integration/ directory structure and meson.build Python test runner (subprocess, Python stdlib unittest)
- [X] T057 Create tests/integration/conftest.py with sandboxed temp-dir fixture: HOME, XDG_*, REPRO_SYSROOT, REPRO_TEST_ASSUME_ROOT setup; cleanup
- [X] T058 [P] Create tests/integration/stubs/{locale-gen,udevadm,useradd,usermod} bash logging stubs for system pass testing
- [X] T059 Create tests/fixtures/repos/basic/ config repo (one module, files+env, empty requires)
- [X] T060 [P] Create tests/fixtures/plugins/fake-packages.lua pure-Lua backend for testing (probe, installed, install stubs)
- [X] T061 [P] Create tests/fixtures/plugins/fake-services.lua pure-Lua backend for testing (probe, state, enable stubs)

**Checkpoint**: Phase 2 complete. All unit and Lua tests pass. SQLite DB schema created on first run. Lua bootstrap loads and sandbox restrictions enforced. Error messages structured. Platform abstraction tested with fakes.

---

## Phase 3: User Story 1 — Declare Once, Apply Safely (M1, Priority: P1) 🎯 MVP

**Goal**: Users scaffold a config repository, declare files and environment variables, preview the plan, apply in user mode, and re-run as a no-op. Drift is detected and restored without prompting.

**Independent Test**: `tests/fixtures/repos/basic/` with one module declaring a file, an env var, and a package; run `--dry-run` (plan shown, nothing changed), apply (changes present), re-apply (up to date, < 2 s, zero changes).

**Acceptance from spec.md**:
1. Previewed plan shows changes; machine unchanged.
2. Applied run materializes all declared files/env; summary lists changes; adopts existing unmanaged files; drift is restored.
3. Re-apply is fast no-op (target < 2 s for tens of modules).
4. Existing file not managed → refusal with `--adopt` hint.
5. Drifted managed file → restored without prompt.

### Implementation

#### Discovery & Evaluation

- [X] T062 [US1] Implement src/eval/discovery.cpp finish: load repro.lua, extract options, profiles, providers from RootConfig (research R11)
- [X] T063 [US1] Implement src/eval/evaluate.cpp finish: eval all modules in sandbox, apply `with_params` per-profile, build resolved declaration tree
- [X] T064 [US1] Create tests/lua/test_merge.lua with luaunit for `with_params` fragment merging (list concat, map merge, conflict error)

#### Planning Pipeline

- [X] T065 [US1] Implement src/plan/resolver.cpp for interface resolution state machine (research R16): cli override → profile → pin → lock → only-option → prompt → E_AMBIGUOUS; record source
- [X] T066 [US1] Implement src/plan/builder.cpp: synthesize Step objects for files/env per module, assign privilege (files: user if under home else system; env→user, system_env→system per research R12), set check/description
- [X] T067 [US1] Implement src/plan/plan.cpp finish: invoke discovery, eval, resolution, graph, builder; return ordered Plan
- [X] T068 [US1] Create tests/unit/test_resolver.cpp with doctest for precedence order, only-option auto-selection

#### Step Checking & Applying

- [X] T069 [US1] Implement src/apply/checks.hpp and src/apply/checks.cpp: for files, hash stored vs on-disk (adoption guard); for env, check if file exists
- [X] T070 [US1] Implement src/apply/files.cpp step executor: link/copy/create with mode/chown, atomic write (tmp+fsync+rename per research R20), populate managed_files ledger entry
- [X] T071 [US1] Implement src/apply/env.cpp step executor: write `90-repro-<slug>.conf` per module (research R13), check no duplicate env vars across modules, atomically — *env/udev executors share src/apply/files.cpp; per-module env files + conflict check in src/plan/builder.cpp*
- [X] T072 [US1] Implement src/apply/engine.cpp: execute steps in Plan order, re-check before mutate (TOCTOU), commit ledger per step, stop at first failure
- [X] T073 [US1] Implement adoption guard in src/apply/adoption.cpp: detect unmanaged existing file, refuse with E_BLOCKED + `--adopt` hint, keep backups (`<dest>.repro-adopted-<timestamp>`), journal adoption (research R14)
- [X] T074 [US1] Create tests/unit/test_apply.cpp with doctest for step execution, adoption guard, env dedup, link vs copy

#### CLI Commands

- [X] T075 [US1] Implement `repro init`: scaffold `~/.config/repro/` (repro.lua stub, modules/, files/, plugins/lua/) with starter modules (e.g., "base" for system.env + users)
- [X] T076 [US1] Implement `repro sync [-u | -s] [--dry-run] [--yes] [--adopt] [--refresh MODULE] [modules...]`: resolve scope, plan, preview or apply per research R15; exit 0 (ok), 1 (apply failed), 2 (config error, E_BLOCKED)
- [X] T077 [US1] Implement `repro status [mode]`: per-module status (applied, stale, pending, failed) from module_state table per mode (research R10)
- [X] T078 [US1] Implement `repro journal [--mode MODE] [--module MODULE]`: readable output of run/step history from DB; timestamps, results, error reasons, adopted files (research FR-012)

#### Fingerprint & Idempotency

- [X] T079 [US1] Integrate fingerprint computation in plan phase; compare against module_state.fingerprint; mark steps `skipped-fp` for actions only if FP matches (research R10)
- [X] T080 [US1] Implement drift detection in checks: compare on-disk file hash vs ledger; if managed and differs, return `needs-change` for restore; if unmanaged and different, return `blocked` (research US1-AS5)

#### Preview & Output

- [X] T081 [US1] Implement src/cli/preview.cpp: render plan to stdout with group-by-module, deterministic order (no PIDs, temp paths, timestamps per research R19), step descriptions, adoption warnings (research FR-009, contracts/cli.md)
- [X] T082 [US1] Implement result summary: list applied/skipped/failed counts, changed files, changed env vars, drift restored (research FR-012)

#### Integration Tests

- [X] T083 [US1] Create tests/integration/test_us1_declare.py: scaffold, declare module (file + env + fake package), preview (plan shown, nothing changed), apply (changes present, summary listed), re-apply (fast, zero changes)
- [X] T084 [US1] Create tests/integration/test_us1_adoption.py: existing unmanaged file → E_BLOCKED + --adopt suggestion; --adopt succeeds, backup created, journaled
- [X] T085 [US1] Create tests/integration/test_us1_drift.py: apply, drift file, re-apply, drift restored without prompt
- [X] T086 [US1] Create tests/integration/test_perf.py: 40-module fixture, second `sync -u` < 2 s (SC-002)
- [X] T087 [US1] Create tests/integration/test_determinism.py: two `--dry-run` runs byte-identical (SC-003); with hash-seed perturbation

**Checkpoint**: User Story 1 complete and independently testable. All acceptance scenarios pass. MVP deliverable: users can scaffold, declare, preview, apply, and re-run files/env safely.

---

## Phase 4: User Story 2 — Compose: Dependencies and Interchangeable Providers (M2, Priority: P2)

**Goal**: Modules declare requirements that are resolved in dependency order. Interfaces can have multiple providers; the user is prompted once, and the choice is recorded in `repro.lock.lua` so future runs never ask again.

**Independent Test**: Fixture with requirer A, interface I, providers P1 (priority 10) and P2 (20). Run in sandbox: first apply prompts once (default P1), choice recorded in lock; re-apply performs no prompt, same provider; explicit override via `--provider` changes selection and lock source.

**Acceptance from spec.md**:
1. Module A requires module B → B applied before A in deterministic order.
2. Unselected interface with two providers → prompted once with names/descriptions, default highlighted (priority), choice recorded.
3. Recorded choice → no prompt on subsequent runs.
4. Explicit `--provider` override → recorded with source=cli.
5. Non-interactive (--yes) + ambiguous → E_AMBIGUOUS exit 2, candidates listed.
6. Cycle among modules → E_CYCLE at plan time, chain displayed.

### Implementation

#### Graph & Cycle Detection

- [X] T088 [US2] Implement src/plan/cycles.cpp: DFS-based cycle detection (research R10), return E_CYCLE with chain on cycle, or ordered closure — *cycle detection lives in src/plan/graph.cpp (topo_order)*
- [X] T089 [US2] Enhance tests/unit/test_plan.cpp with cycle detection tests (simple cycle, multi-node, self-loop)

#### Provider Prompt & Lock File

- [X] T090 [US2] Implement src/cli/prompt.cpp: TTY detection (`/dev/tty`, isatty checks), list providers with priority-based default highlight, interactive selection via stdin
- [X] T091 [US2] Implement src/plan/lock.hpp and src/plan/lock.cpp: read/write `repro.lock.lua` (Lua file format per data-model.md §3.2), merge prompt-selected decisions into lock with `decided_at` timestamp, handle `--reselect` flag (research R16)
- [X] T092 [US2] Create tests/lua/test_lock.lua with luaunit for lock file I/O (parse, serialize, merge)

#### CLI Flags

- [X] T093 [US2] Implement `--provider IFACE=PROVIDER` flag (repeatable) for explicit override per research R16, precedence level 1
- [X] T094 [US2] Implement `--reselect` flag to ignore lock and re-prompt
- [X] T095 [US2] Implement `--yes` flag for automation: all prompts error → E_AMBIGUOUS if unresolved

#### New CLI Commands

- [X] T096 [US2] Implement `repro list providers [--module MODULE]`: show all available providers, their names, descriptions, origins (repo pin, per-profile, etc.)
- [X] T097 [US2] Implement `repro show MODULE`: resolved declaration of MODULE (parameters, providers, content sections) in canonical format (research R8)

#### Integration Tests

- [X] T098 [US2] Create tests/fixtures/repos/providers/ with interface dev/ide (providers dev/ide/vim priority 10, dev/ide/emacs priority 20) and requirer dev/c++
- [X] T099 [US2] Create tests/integration/test_us2_compose.py: apply requirer (prompt for interface, default vim), lock written, re-apply (no prompt, same provider)
- [X] T100 [US2] Create tests/integration/test_us2_override.py: explicit `--provider dev/ide=dev/ide/emacs` override, lock source=cli
- [X] T101 [US2] Create tests/integration/test_us2_cycles.py: repo with cycle → E_CYCLE, chain displayed
- [X] T102 [US2] Create tests/integration/test_us2_noninteractive.py: `--yes` with ambiguous interface → E_AMBIGUOUS exit 2, candidates listed
- [X] T103 [US2] Create tests/integration/test_providers.py: 10 consecutive `--yes` runs after one prompt, 0 prompts (SC-006)

**Checkpoint**: User Story 2 complete. Provider resolution, locking, and prompting work. Cycles detected. Both stories independently functional.

---

## Phase 5: User Story 3 — Root-Side Configuration in Separate Pass (M3, Priority: P3)

**Goal**: Privileged steps (packages, system files, services, users, locale, udev) are separated from user steps. A `--system` / `-s` flag applies only system work; unprivileged runs report pending system changes. Each mode has its own DB and execution records.

**Independent Test**: Repo with both user and system steps. User pass unprivileged (user applied, system reported pending). System pass elevated via `REPRO_TEST_ASSUME_ROOT=1` (system applied, user untouched). Re-run each independently (both skip).

**Acceptance from spec.md**:
1. User pass → only user steps applied, system count reported pending with elevated-pass hint.
2. System pass → only system steps applied, user files untouched.
3. System pass without privileges → E_PRIVILEGE error + hint for `sudo repro sync -s`.
4. Both passes complete → each independently skips on re-run.
5. System-only module → applied in system pass, recorded separately.

### Implementation

#### Privilege Classification

- [X] T104 [US3] Implement src/plan/privilege.cpp: deterministic privilege classification per step (research R12): files (user if dest under home else system), env/system_env (user/system), packages (from backend), services (scope field), users/locale/udev (system), actions (from mode field) — *src/plan/privilege.cpp; classification applied in builder.cpp*
- [X] T105 [US3] Implement privilege filtering in plan phase: split steps into two ordered lists (user, system) per run.mode

#### Separate State Stores

- [X] T106 [US3] Enhance src/state/db.cpp to open per-mode: user `~/.local/state/repro/repro.db`, system `/var/lib/repro/repro.db` (0700 dir, 0600 file, root-owned per research R5)
- [X] T107 [US3] Implement separate run.lock per state dir (research R6)
- [X] T108 [US3] Enhance module_state queries to be mode-specific

#### Privilege Checks & Hints

- [X] T109 [US3] Implement src/platform/users.cpp SUDO_USER detection: under `sudo`, use `getpwnam($SUDO_USER)` for home/config resolution (research R12)
- [X] T110 [US3] Implement privilege check in apply phase: if mode=system and euid != 0 (unless REPRO_TEST_ASSUME_ROOT=1), error E_PRIVILEGE with hint `sudo repro sync -s` (research R12)
- [X] T111 [US3] Implement "pending other-mode" reporting: in preview/status, count system steps pending when in user mode and vice versa, add hint (research US3-AS1)

#### System Step Executors

- [X] T112 [US3] Implement src/apply/users.cpp: useradd/usermod via Proc.spawn, getent validation, additive-groups-only policy (research data-model.md UserEntry)
- [X] T113 [US3] Implement src/apply/locale.cpp: managed block (# BEGIN repro / # END repro) in `/etc/locale.gen`, `locale-gen` post-step, `/etc/locale.conf` LANG= if default declared (research R13, data-model.md LocaleEntry)
- [X] T114 [US3] Implement src/apply/udev.cpp: write per-rule files to `/etc/udev/rules.d/`, single `udevadm control --reload` post-step if any udev changed (research R13) — *udev rule files use the files executor; reload post-step in src/apply/locale.cpp (apply_post)*
- [X] T115 [US3] Enhance src/apply/files.cpp to handle system scope (absolute paths, root-owned if needed)

#### CLI Updates

- [X] T116 [US3] Add `-s` / `--system` flag to `sync`, `status`, `journal` commands (mode selection)
- [X] T117 [US3] Default mode (no flag) = user mode
- [X] T118 [US3] Update `repro status` to default user mode; show pending other-mode count
- [X] T119 [US3] Update `repro journal` to filter by mode

#### Integration Tests

- [X] T120 [US3] Create tests/fixtures/repos/mixed/ with both user (files, env) and system (users, locale) modules
- [X] T121 [US3] Create tests/integration/test_us3_privilege.py: user pass (user steps applied, system reported pending), system pass elevated (system steps applied), both independent on re-run
- [X] T122 [US3] Create tests/integration/test_us3_noprivilege.py: `sync -s` unprivileged → E_PRIVILEGE with sudo hint
- [X] T123 [US3] Create tests/integration/test_concurrency.py: two same-mode syncs → one exit 2 E_BUSY, one proceeds; cross-mode both proceed (SC-009)

**Checkpoint**: User Story 3 complete. Privilege separation working. System and user passes independent. Three stories now functional together.

---

## Phase 6: User Story 4 — Named Profiles for Different Setups (M4, Priority: P4)

**Goal**: A repo serves multiple setups (laptop, server, base). Named profiles select module subsets with optional parameter and provider overrides. One profile marked default; runs without explicit profile selection use the default.

**Independent Test**: Repo with two profiles (server, laptop) sharing core modules but with different parameters. Run each profile, verify only that profile's modules selected; parameters change plan.

**Acceptance from spec.md**:
1. Default profile declared → omit flag, use default modules.
2. Multiple profiles, no default → omit flag error, list profiles.
3. Selected profile → only its modules (plus closures) planned.
4. Per-profile parameter override → value appears in plan.
5. Per-profile provider override → precedence level 2, plan reflects (research R16).

### Implementation

#### Parameter Resolution & Validation

- [X] T124 [US4] Implement src/plan/params.cpp: resolve parameter values per module (priority: CLI override → profile value → declared default → error if required but missing per research R11), validate against type/one_of — *param resolution/validation is in Lua (params.lua); C++ passes profile overrides through Evaluator::resolve*
- [X] T125 [US4] Create tests/unit/test_params.cpp with doctest for validation, type coercion, constraint checking
- [X] T126 [US4] Create tests/lua/test_params.lua extension with `with_params` integration tests

#### Profile Selection & Merging

- [X] T127 [US4] Implement src/plan/profile.cpp: discover profiles from RootConfig, validate (name format, at most one default per research data-model.md Profile), select by CLI flag or default — *profile selection in src/cli/pipeline.cpp (select_roots); validation in builders.lua/init.lua*
- [X] T128 [US4] Implement profile-scoped parameter/provider override merging into plan context
- [X] T129 [US4] Implement scope selection variant: if no CLI modules given, use selected profile's `use` list, expand closure

#### New CLI Commands

- [X] T130 [US4] Implement `repro list profiles`: show all profiles, highlight default, show description
- [X] T131 [US4] Implement `--profile PROFILE` flag for `sync` command

#### Integration Tests

- [X] T132 [US4] Create tests/fixtures/repos/profiles/ with server/laptop profiles, shared and unique modules, per-profile parameter/provider overrides
- [X] T133 [US4] Create tests/integration/test_us4_profiles.py: apply each profile, verify correct module subsets and parameter values in plan
- [X] T134 [US4] Create tests/integration/test_us4_default.py: default profile auto-selected; no-default with multiple profiles → error listing profiles

**Checkpoint**: User Story 4 complete. Profiles, parameters, and per-profile overrides work. Four stories integrated.

---

## Phase 7: User Story 5 — Swap Domain Engines Without Changing Core (M5, Priority: P5)

**Goal**: Pluggable backend add-ons (packages, services) loaded from system paths or repo-local Lua. Core works with zero add-ons (files/env only). Missing backend → actionable error. User can list all loaded extensions.

**Independent Test**: Sandboxed fixture with stub/fake backends (pure Lua + repo-local). Base operation without backends (files/env succeeds). Package step without backend → E_NO_BACKEND naming repro-pacman. Installed fake backend auto-selects, listed in plugins list.

**Acceptance from spec.md**:
1. Filesystem/environment only, zero backends → succeeds.
2. Required backend missing → E_NO_BACKEND + hint (install repro-pacman).
3. Eligible backend installed, not configured → auto-selects (research R17), stated in preview.
4. Repo-local lightweight extension → enabled, visibly reduced-trust (research R17).
5. List extensions: kind, name, origin, version, incompatible versions rejected with both versions.
6. Declarative contract violation (mutations during eval) → E_DECLARATIVE with guidance (research R9).

### Implementation

#### Plugin System & ABI

- [X] T135 Create include/repro/plugin.h: C ABI header, `REPRO_PLUGIN_ABI = 1`, entry `repro_plugin_init(abi, host, host_api, registrar, registrar_api)`, data is NUL-terminated UTF-8 strings only (research R17)
- [X] T136 [P] Create src/plugins/loader.hpp and src/plugins/loader.cpp: dlopen with `RTLD_NOW|RTLD_LOCAL`, search path (system `/usr/lib/repro/plugins`, `/usr/lib64/repro/plugins`, user `~/.local/lib/repro/plugins`, repo-local `<config>/plugins/lua/*.lua` per research R17), ABI version check, fail-soft per plugin

#### Backend Facades

- [X] T137 [US5] Enhance src/lua/repro/backend.lua: dispatch packages/services namespaces to native .so or repo-local Lua backends, call routing (probing, installed, install, state, etc.)
- [X] T138 [US5] Implement step executors for packages and services calling backend ops via facade

#### Backend Abstraction for Steps

- [X] T139 [US5] Implement src/apply/packages.cpp step executor: call backend.install(names), record in managed_packages ledger (research data-model.md managed_packages)
- [X] T140 [US5] Implement src/apply/services.cpp step executor: call backend.enable/disable/start/stop per ServiceEntry
- [X] T141 [US5] Implement missing-backend detection and error in plan phase (E_NO_BACKEND): check required namespace has at least one candidate, error message includes distro-specific install hint (research R17)

#### Backend Auto-Selection & Probe

- [X] T142 [US5] Implement backend auto-probing: all probing-positive backends per namespace ranked by (origin: system > user-dir > repo-lua, then name), first wins, stated in preview (research R17)
- [X] T143 [US5] Implement backend config override in RootConfig options: `options.packages.backend`, `options.services.backend` (values: "auto" or backend name)

#### New CLI Commands

- [X] T144 [US5] Implement `repro plugins list`: show all loaded extensions: kind (packages/services), name, origin (system/user/repo), version, abi status (compatible/incompatible with both versions)

#### Pacman & Systemd Add-Ons

- [X] T145 Create addons/pacman/src/main.cpp with pacman backend plugin: namespace=packages, probe (pacman -V), installed (pacman -Qq), install (pacman -S), remove (pacman -R)
- [X] T146 Create addons/pacman/meson.build shared_module target, installed to libdir/repro/plugins/pacman.so
- [X] T147 Create addons/systemd/src/main.cpp with systemd backend plugin: namespace=services, probe (systemctl --version), state (systemctl is-enabled/is-active), enable/disable/start/stop
- [X] T148 Create addons/systemd/meson.build shared_module target, installed to libdir/repro/plugins/systemd.so

#### Declarative Contract Enforcement

- [X] T149 [US5] Enhance src/eval/sandbox.cpp to detect mutating `rp.*` calls during EVAL phase (research R9), error E_DECLARATIVE with guidance to declare instead — *enforced in the Lua facades (backend.lua, init.lua ctx.exec), not a separate sandbox.cpp*

#### Integration Tests

- [X] T150 [US5] Create tests/integration/test_us5_no_backends.py: files-only repo, zero plugins installed, succeeds
- [X] T151 [US5] Create tests/integration/test_us5_missing_backend.py: packages required, no pacman backend, E_NO_BACKEND exit 2 names repro-pacman
- [X] T152 [US5] Create tests/integration/test_us5_backends.py: with fake backends, package steps execute, listed in `plugins list`
- [X] T153 [US5] Create tests/integration/test_us5_repo_local.py: repo-local `plugins/lua/fake-*.lua` backend, enabled and flagged reduced-trust

**Checkpoint**: User Story 5 complete. All five stories functional. Core works with/without backends. Plugin system extensible.

---

## Phase 8: Polish & Hardening (M6 Hardening — Post-MVP)

**Purpose**: Performance optimization, determinism validation, resume/interruption handling, error catalogue completion, documentation, and cross-cutting improvements.

**Gate**: All user story phases must be complete before hardening work begins.

### Documentation & API Contract

- [X] T154 [P] Write docs/lua-api.md: normative user documentation of `rp.*` API surface (all functions, parameters, return values, error guarantees); kept in sync by test (research R18)
- [X] T155 Create tests/lua/test_api_docs.lua: iterate bootstrap's `rp` API registry, assert each symbol in docs/lua-api.md (research R18)
- [X] T156 [P] Complete contracts/cli.md: all exit codes (0, 1, 2, 130), error messages, plan/status output format, journal format — *contracts updated with implemented details (status line, plan-error, post-steps, extra codes)*
- [X] T157 [P] Complete contracts/lockfile.md: `repro.lock.lua` full format specification per data-model.md §3.2
- [X] T158 [P] Complete contracts/plugin-abi.md: full C ABI spec, dlopen contract, return value conventions

### Performance & Determinism

- [X] T159 Create tests/integration/test_perf_final.py: 40-module fixture, second `sync -u` < 2 s (SC-002 validation with real repo) — *covered by tests/integration/test_perf.py (median of 5, 40 modules) — no separate _final file*
- [X] T160 Create tests/integration/test_determinism_final.py: two `--dry-run` runs with hash-seed perturbation, byte-identical plans (SC-003) — *covered by tests/integration/test_determinism.py — no separate _final file*

### Interruption & Resume

- [X] T161 [P] Implement signal handling in src/apply/signals.cpp: SIGINT/SIGTERM set flag, checked between steps, forwarded to running child, run closed with exit 130 (research R15)
- [X] T162 [P] Implement resume semantics: incomplete steps re-attempted on re-run (research R15)
- [X] T163 Create tests/integration/test_resume.py: SIGINT injected after step k via fake backend, re-run completes, journal shows no duplicates (SC-004)

### CLI Polish

- [X] T164 [P] Implement colored output for terminal (isatty(1) check, NO_COLOR respect per research R19)
- [X] T165 [P] Implement `--help` per command with CLI11's built-in
- [X] T166 [P] Ensure all error messages name element (module/step/interface) and suggest action (research FR-024, SC-007)

### Test Suites & CI

- [X] T167 [P] Complete tests/integration/test_errors.py with full error catalogue (all E_* codes from contracts/cli.md, each with element + hint regex)
- [X] T168 [P] Implement CI matrix: GCC 15+, Clang 19+, Arch, with `-Werror`, `meson test --suite unit/lua/integration` — *.github/workflows/ci.yml (not run here); GCC 16 and Clang 22 -Werror builds verified locally*
- [X] T169 [P] Implement formatting gate: `ninja -C build clang-format-check` in CI

### Adoption & Distribution

- [X] T170 Ensure repro-pacman and repro-systemd add-ons build independently via `meson install`
- [X] T171 Create install packages/instructions for Arch (PKGBUILD or ArchWiki pattern) — *packaging/arch/PKGBUILD (split package; not built with makepkg here)*

### Final Validation

- [X] T172 Run quickstart.md full walkthroughs: US1–US5 manual scenarios, verify all outcomes match spec — *walkthroughs exercised by the integration suite; quickstart.md fixed for needs-pkgs*
- [X] T173 Validate all success criteria (SC-001 through SC-009) via automated + manual tests
- [X] T174 Review all error messages for completeness and clarity (SC-007)

**Checkpoint**: All phases complete. Feature ready for MVP release. All 5 user stories independently testable. Performance, determinism, and resume semantics validated. Documentation complete.

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup/M0)
  ↓
Phase 2 (Foundational/M0-M1-early)
  ↓
Phase 3 (US1) ─┐
               ├─→ Phase 4 (US2) ─┐
Phase 3 & 4 ──→ Phase 5 (US3) ─┤
               ├─→ Phase 6 (US4) ┤
               └─→ Phase 7 (US5) ─┴─→ Phase 8 (Hardening/Polish)
```

**Critical Path**: 1 → 2 → 3 (or 3+4+5+6+7 in parallel after 2)

### Parallel Opportunities

1. **Phase 1 Setup**: All [P] tasks (platform layer, lua bootstrap, test infra) can run in parallel (different files).
2. **Phase 2 Foundational**: C++ models, SQLite, Lua host can develop in parallel.
3. **User Stories 1–5**: After Phase 2 complete, each story is independently compilable and testable; assign to team members.
4. **Within each story**: Tests written first (parallel per test file), then implementation (parallelizable per component).

### Suggested Parallel Example (Team of 3)

```
Developer A: Phase 1 + US1 (core delivery)
Developer B: Phase 1 + US2 + US3 (composition & privilege)
Developer C: Phase 1 + US4 + US5 (profiles & extensibility)
After all: Phase 8 polish together
```

---

## Implementation Strategy

### MVP First (User Story 1 Only — 3–4 weeks solo)

1. **Weeks 1–1.5**: Phase 1 Setup + Phase 2 Foundational (Meson, platform, SQLite, Lua bootstrap)
2. **Weeks 1.5–3**: Phase 3 User Story 1 (discovery, eval, files, env, plan, apply, init/sync/status/journal)
3. **Week 3.5**: Validate against spec's US1 acceptance scenarios + quickstart US1 walkthroughs
4. **→ Ship**: Single-story MVP: scaffold, declare, preview, apply, re-run

### Incremental Delivery (Full Feature — 6–8 weeks, assume one developer or staggered team)

**Weeks 1–1.5**: Phase 1 + Phase 2
**Weeks 1.5–3**: Phase 3 (US1)
**Weeks 3–4**: Phase 4 (US2, requires/providers/prompt/lock)
**Weeks 4–5**: Phase 5 (US3, privilege/system-pass)
**Weeks 5–6**: Phase 6 (US4, profiles)
**Weeks 6–7**: Phase 7 (US5, backends)
**Weeks 7–8**: Phase 8 (hardening, docs, tests, perf)

**→ Ship**: Full MVP feature with all 5 user stories.

### Multi-Developer Strategy (3 devs, 4–5 weeks)

**Weeks 1–1.5** (all together):
- Dev A, B, C: Phase 1 Setup, Phase 2 Foundational (parallel on non-conflicting files)

**Weeks 1.5–3** (all together):
- Dev A: Phase 3 User Story 1
- Dev B: Phase 4 User Story 2 (draft, non-blocking)
- Dev C: Phase 5 User Story 3 (draft, non-blocking)

**Week 3** (integrate):
- Merge US1 complete
- US2/US3 refinement as dependencies resolve

**Weeks 3.5–4**:
- Dev A: Phase 6 US4
- Dev B: Phase 7 US5 (requires backend design finalization)
- Dev C: Early Phase 8 docs + test infra

**Week 4+**: Final integration, Phase 8 polish, ship

---

## Notes

- **[P] marker**: Tasks with different files, no inter-task dependencies; can run in parallel if staffed.
- **[US#] label**: Denotes which user story a task belongs to (Phase 3–7 only). Enables independent story tracking.
- **File paths**: All relative to repository root per plan.md project structure; e.g., `src/eval/`, `tests/`, `addons/`, `docs/`.
- **Test strategy**: Per Constitution IV, all three test suites (unit, lua, integration) required; tests written before implementation.
- **Checkpoints**: Each user story and phase has a checkpoint; stop and validate independently before proceeding.
- **Success**: All 5 user stories complete, independently testable, pass acceptance scenarios from spec.md, all SC-001 through SC-009 success criteria validated.
