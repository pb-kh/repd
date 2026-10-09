<!--
SYNC IMPACT REPORT
==================
Version change: (uninitialized template) → 1.0.0
Bump rationale: Initial ratification. All placeholder tokens replaced with
concrete principles derived from the project brief (C++23 / Meson,
NixOS-like declarative environment, LuaJIT-embedded configuration).

Modified principles: (none - first fill)
  - [PRINCIPLE_1_NAME] → I. Declarative & Reproducible Configuration
  - [PRINCIPLE_2_NAME] → II. Lua/LuaJIT as the Configuration Surface
  - [PRINCIPLE_3_NAME] → III. C++23 Core, Hermetic Meson Builds
  - [PRINCIPLE_4_NAME] → IV. Test-First (NON-NEGOTIABLE)
  - [PRINCIPLE_5_NAME] → V. Safe, Idempotent & Observable Apply Operations

Added sections:
  - Additional Constraints (technology stack & platform policy)
  - Development Workflow & Quality Gates

Removed sections: (none)

Templates status:
  - .specify/templates/plan-template.md ✅ (generic "Constitution Check"
    gate section; no outdated references - gates are derived from this file)
  - .specify/templates/spec-template.md ✅ (no constitution-specific content)
  - .specify/templates/tasks-template.md ✅ (generic; no constitution-specific
    content)
  - .specify/templates/commands/*.md ✅ (directory does not exist yet)
  - README.md / docs ⚠ pending (no README or docs exist yet; create runtime
    guidance referencing these principles when the first source lands)

Follow-up TODOs: (none - no deferred placeholders)
-->

# repro Constitution

## Core Principles

### I. Declarative & Reproducible Configuration

repro is a NixOS-like environment tool: users declare the desired state of
their system in configuration files, and `repro` applies those declarations.
The final system state MUST be a deterministic function of (configuration,
current machine state); hidden or ambient state MUST NOT influence the
result. Applying the same configuration to the same starting state MUST
produce the same end state (idempotence). Configuration MUST be declarative
in intent ("what the system looks like"), not imperative ("what steps to
run"); any imperative escape hatch MUST be explicitly marked as such in the
API and documentation. Rationale: reproducibility is the product's reason
to exist - a violation here is a product defect, not a style issue.

### II. Lua/LuaJIT as the Configuration Surface

All user-facing configuration and user extension logic MUST be written in
Lua and executed by the LuaJIT runtime embedded in the `repro` binary.
Users MUST NOT need to rebuild or recompile `repro` to change, extend, or
customize environment behavior. The Lua API exposed to configuration
scripts is a public contract: it MUST be documented, and breaking changes
to it MUST follow the versioning policy in the Governance section. C++
core code MUST NOT parse or evaluate configuration directly; all
configuration passes through the LuaJIT evaluation layer. Rationale: one
scripting surface keeps the mental model small and makes user scripts
portable across repro versions.

### III. C++23 Core, Hermetic Meson Builds

The core implementation MUST be written in C++23 and built exclusively
with Meson (Ninja backend). New code MUST compile with C++23 features
enabled and MUST NOT introduce additional build systems, code generators,
or meta-build wrappers. Builds MUST be hermetic: dependency versions are
pinned via Meson subprojects or an explicit manifest, and no build step
MUST depend on network access, system-wide mutable state, or the local
user environment. Compiler warnings MUST be treated as errors in CI.
Rationale: a tool whose promise is reproducibility loses credibility if
its own build is not reproducible.

### IV. Test-First (NON-NEGOTIABLE)

Tests MUST be written before implementation and MUST be registered in the
Meson test suite (`meson test`). Every user-visible behavior change (new
Lua API function, new configuration directive, new apply behavior) MUST
land with a failing test first, then the implementation that makes it
pass. Config parsing, evaluation, and apply semantics MUST each have
dedicated unit tests; end-to-end apply scenarios MUST have integration
tests that run against a sandboxed fixture environment, never a live
system. The full test suite MUST pass before any change is merged.
Rationale: apply operations mutate real machines; untested behavior in
this domain is unacceptable risk.

### V. Safe, Idempotent & Observable Apply Operations

`repro` applies configurations to real systems, so apply MUST be safe by
construction:

- A preview/dry-run mode MUST be available for every apply operation,
  showing exactly what will change before anything changes.
- Apply MUST be idempotent: re-applying an already-applied configuration
  MUST be a no-op.
- Destructive or partially-applied operations MUST be either transactional
  or resumable/rollback-safe; `repro` MUST NOT leave a system in a state
  that cannot be recovered from or continued.
- Every apply run MUST produce a structured, auditable log of what was
  changed, in what order, and with what result.

Rationale: the difference between a configuration tool and a liability is
whether users trust it with their machines.

## Additional Constraints

Technology stack policy:

- **Language**: C++23 (GCC or Clang with full C++23 support). No C-style
  headers-only reuse of legacy patterns; new code MUST use modern C++
  idioms.
- **Build system**: Meson >= 1.x with the Ninja backend. Meson is the only
  supported entry point for building, testing, and installing.
- **Scripting runtime**: LuaJIT (Lua 5.1-compatible with LuaJIT
  extensions). The embedded LuaJIT version MUST be pinned and the pin
  recorded in the build manifest.
- **Target platform**: Linux first, with NixOS as the primary reference
  platform. Platform-specific code MUST be isolated behind clear
  abstraction boundaries so other POSIX targets can be added without
  rewriting the core.

Constraints on scope:

- `repro` MUST remain a single self-contained binary with no runtime
  dependency on external interpreters or package managers other than what
  it explicitly manages.
- User configuration files MUST be plain Lua source (or Lua source in a
  repro-specific layout); no proprietary configuration format may be
  introduced without amending this constitution.

## Development Workflow & Quality Gates

- **Branching**: All work happens on feature branches; direct commits to
  the primary branch are not allowed except for documentation-only fixes.
- **Commit discipline**: Commit after each task or logical group, using
  conventional-commit style messages (`feat:`, `fix:`, `docs:`, `test:`,
  `refactor:`).
- **Quality gates** (all MUST pass before merge):
  1. `meson test` - full unit and integration suite green.
  2. Zero compiler warnings with warnings-as-errors enabled.
  3. Formatted code (clang-format or the project's declared formatter)
     with no diffs.
  4. New Lua API surface documented alongside its tests.
- **Simplicity**: Prefer the simplest implementation that satisfies the
  current requirement (YAGNI). Any added abstraction, dependency, or
  build complexity MUST be justified in the implementation plan's
  Complexity Tracking section.
- **Review**: Every change to the Lua API contract or apply semantics
  MUST be reviewed against Principles I, II, and V before merge.

## Governance

- This constitution is the highest authority for the `repro` project; it
  supersedes any conflicting practice, plan, or ad-hoc decision.
- **Amendments** MUST be documented in this file with: the principle(s)
  changed, the rationale, and a migration plan for anything the change
  breaks (including user-facing Lua configuration compatibility).
- **Versioning**: The constitution version follows semantic versioning.
  - MAJOR: removal or redefinition of a principle (incompatible
    governance change).
  - MINOR: addition of a principle or materially expanded guidance.
  - PATCH: clarifications, wording, and non-semantic refinements.
- **Compliance review**: The Constitution Check gate in
  `.specify/templates/plan-template.md` MUST be evaluated against the
  current version of this file at plan time and re-checked after design.
- Runtime development guidance (README, quickstart, docs/) MUST stay
  consistent with this file; where they conflict, this file wins and the
  docs MUST be updated.

**Version**: 1.0.0 | **Ratified**: 2026-10-09 | **Last Amended**: 2026-10-09
