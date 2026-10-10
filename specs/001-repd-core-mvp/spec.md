# Feature Specification: repd — Declarative Environment Configuration Core

**Feature Branch**: `001-repd-core-mvp`

**Created**: 2026-10-09

**Status**: Draft

**Input**: User description: "Users write their entire environment configuration in one place as scripted, declarative config files; repd applies it. Configuration is modular (partial application), modules declare dependencies, interfaces may have multiple providers that the user is asked about once, domain engines (package/service managers) are pluggable add-ons, and every run records execution info so re-running only does what is still needed." (Gathered interactively in REQUIREMENTS.md; see that document for design-level context.)

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Declare the environment once, apply it safely (Priority: P1)

A user sets up a repd configuration repository for their machine. They declare a
small set of environment modules — things like "which packages my base system
uses", "where my dotfiles live", "which environment variables my shell needs" —
in declarative files. They run a preview that shows exactly what repd intends to
change, then apply in user mode. Their declared state materializes on the
machine. Running the same command again is a fast no-op: nothing is re-applied.

**Why this priority**: This is the product's core promise (declarative → applied,
idempotent). Without this slice nothing else has a user value.

**Independent Test**: In an isolated test machine/home: scaffold a config
repository, write one module declaring a file, an env var, and a package; run
preview (plan shown, nothing changed); apply (changes present); run again
("up to date", zero changes performed). Fully testable without any other story.

**Acceptance Scenarios**:

1. **Given** an empty home and a freshly scaffolded config repository, **When** the user runs a preview, **Then** the tool shows every intended change grouped by module in dependency order, and **the target machine is unchanged**.
2. **Given** a previewed plan, **When** the user applies in user mode, **Then** all declared files, environment variables, and packages reachable by that mode exist in the declared state, and the run summary lists every change it made with its result.
3. **Given** an already-applied, unchanged configuration, **When** the user applies again, **Then** the tool reports each module as up to date, performs **zero** changes, and completes quickly (target: under 2 seconds for typical repos, tens of modules).
4. **Given** a declared destination file that already exists on disk with different content and was never managed by repd, **When** the user applies, **Then** repd refuses to overwrite that entry, explains why, and offers an explicit opt-in adoption path for it; all other steps are unaffected or (default) the run stops with a clear report.
5. **Given** a declared file whose on-disk content drifted after a successful apply, **When** the user re-applies, **Then** repd detects the drift and restores the declared state without manual intervention.

---

### User Story 2 — Compose: dependencies and interchangeable providers (Priority: P2)

A user adds a module for `dev/c++` tooling that requires a module for an editor
environment. Repd resolves dependencies automatically and applies the editor
module's work first. Now, some requirements are *interfaces*: multiple modules
can provide "an IDE-like editor environment" (`dev/ide/vim`, `dev/ide/emacs`, …).
The first time such a choice is needed, repd asks the user, lists the available
providers with their descriptions, and **records the choice** in the
configuration repository so future runs never ask again. The user can override
the choice explicitly (per run, or in the configuration), and in non-interactive
contexts an ambiguous choice is an error rather than a guess.

**Why this priority**: Composition is what makes declarations reusable and keeps
the tool honest about interchangeable technologies; it feeds the recorded-choice
mechanism that keeps automation quiet.

**Independent Test**: Expose a repo with a requirer, an interface, and two
providers; apply the requirer in a sandbox: the prompt appears once, the chosen
provider's content is applied, and a re-run performs no prompt and no repeated
work. Fully independent of Stories 1, 3, 5.

**Acceptance Scenarios**:

1. **Given** module A requires module B, **When** the user applies A, **Then** B's declared changes are applied **before** A's, in a deterministic order.
2. **Given** a required interface with two unselected providers, **When** the user applies interactively, **Then** they are prompted once — with provider names and descriptions, a sensible default (declared priority) highlighted — and the choice is recorded in the repository's decision file.
3. **Given** a recorded provider choice, **When** subsequent runs are executed, **Then** no prompt occurs and the same provider is used.
4. **Given** an explicit command-line override for a provider, **When** the user applies, **Then** the override wins over the recorded choice, and the decision file reflects the new decision with its source noted.
5. **Given** a non-interactive execution (automation mode) and an unresolved provider ambiguity, **When** the user applies, **Then** the run fails with a clear list of eligible providers and **makes no changes**.
6. **Given** a dependency cycle among modules, **When** the user runs anything that plans work, **Then** repd fails at plan time with the offending chain displayed and the machine is unchanged.

---

### User Story 3 — Root-side configuration as a separate privileged pass (Priority: P3)

Some declared state is inherently administrative — system-wide service units,
udev rules, system locale settings, system user accounts, files under system
paths. Repd splits every plan by privilege: a **user pass** applies only
user-privilege work, and a **system pass** (run explicitly with administrator
permission) applies only system-privilege work. Nothing silently escalates. A run
in one mode leaves the other mode's work visibly pending, with a hint about the
other pass. Each mode keeps its own execution records, so they can skip and
progress independently.

**Why this priority**: The two-pass model is a deliberate safety and clarity
decision (user decision 2026-10-09); the tool should never beg for passwords in
the middle of a user-space apply.

**Independent Test**: A repo declaring both user and system steps: run the user
pass unprivileged (user steps applied, system steps reported pending untouched);
run the system pass elevated (system steps applied, records written elsewhere);
re-run each pass (each independently skips). Independent of Stories 2, 4, 5.

**Acceptance Scenarios**:

1. **Given** a configuration with both user and system changes, **When** the unprivileged user pass runs, **Then** only user-privilege changes are applied and the summary states how many system changes remain, with the hint for the elevated pass.
2. **Given** pending system changes, **When** the elevated system pass runs, **Then** only system-privilege changes are applied and user-visible files are not touched.
3. **Given** the tool invoked for the system pass without privileges, **When** it starts, **Then** it refuses with a hint naming the expected invocation and performs no changes.
4. **Given** both passes have previously completed, **When** each pass re-runs, **Then** each independently reports up-to-date with zero rework.
5. **Given** a purely system-scope module (e.g., a udev rule plus a system service declaration), **When** only the system pass runs, **Then** the rule lands in the system rules directory and the declared service state is enforced, with the reload step journaled.

---

### User Story 4 — Named profiles for different setups (Priority: P4)

A single repository can serve several setups (a laptop, a server, a minimal
base): the entry configuration declares named **profiles**, each selecting a set
of modules with per-module parameter overrides and optional provider overrides.
One profile is the default; any run can pick another explicitly. Profiles make
"apply my dev environment, only" a first-class, recorded concept instead of an ad-hoc
argument list.

**Why this priority**: Directly implements the user's requirement of modular,
partial, intentional application from one source of truth.

**Independent Test**: A repo with two profiles sharing modules: run each and
verify only that profile's modules are planned; parameter and provider overrides
change the plan accordingly. Independent of the others given a config repository.

**Acceptance Scenarios**:

1. **Given** a declared default profile, **When** a run omits profile selection, **Then** the default profile's module set is used.
2. **Given** multiple declared profiles and no default, **When** a run omits the flag, **Then** repd fails listing the profiles and the machine is unchanged.
3. **Given** a selected non-default profile, **When** a run executes, **Then** only modules selected by that profile (plus their required closures) are planned.
4. **Given** two profiles that set different values for the same module parameter, **When** either is applied, **Then** the plan reflects the selected profile's value.
5. **Given** provider overrides declared per profile, **When** that profile runs, **Then** the overrides take precedence over repository-wide pins of lesser precedence.

---

### User Story 5 — Swap domain engines without changing the core (Priority: P5)

The core knows *what* should be true about packages and services; it knows
*nothing* about any specific package manager, service manager, platform quirk.
These come from pluggable **backend add-ons** (e.g., one for the distro's
package manager, one for the system service manager), installed as separate
deliverables from normal system channels. The core stays useful with zero
add-ons for all filesystem / environment tasks; a step whose required backend is
missing fails with an actionable message naming the exact add-on to install.
Small, local "experiment" extensions can also live inside the configuration
repository itself (with reduced trust and a warning), mainly useful for exotic
setups and testing. The user can list every loaded extension and where it came
from.

**Why this priority**: Extensibility is the project's longevity argument
(modularity at the application level, per the user brief), but it depends on a
working core, so it closes the MVP.

**Independent Test**: With a sandboxed environment and stub/fake backend
extensions (including a config-repo-local one), verify: base operation without
backends; clear "install add-on X" error when a backend is required but absent;
an installed backend being selected automatically and by explicit setting;
listing shows loaded extensions. Independently demonstrable.

**Acceptance Scenarios**:

1. **Given** a configuration whose steps are filesystem and environment only, **When** it is applied with zero backends installed, **Then** it succeeds without any add-on.
2. **Given** a declared step requiring a backend add-on that is not installed, **When** the plan is built, **Then** repd stops with an error that names the exact add-on (e.g., the distribution package id) and how to install it; already-applied steps remain recorded and untouched.
3. **Given** an eligible backend add-on installed, **When** it is not explicitly configured, **Then** repd auto-selects it and states the choice in the preview; an explicit configuration beats auto-selection.
4. **Given** a lightweight extension declared inside the configuration repository, **When** it is enabled, **Then** its operations behave like a backend, and repd visibly flags its reduced-trust origin in output and in the extension list.
5. **Given** several extensions loaded, **When** the user lists extensions, **Then** each is shown with its kind (namespace), name, origin path, and interface-version status; incompatible-version extensions are refused with both versions printed.
6. **Given** a configuration that attempts to perform changes directly while it is being read (escaping the declarative contract), **When** evaluation proceeds, **Then** repd fails with an explanatory error telling the user to declare intent declaratively instead.

---

### Edge Cases

- **What happens when a declared module name contains invalid characters or doesn't match its file location?** Plan-time error naming the offending entry; nothing is applied.
- **What happens when a required name doesn't exist (typo)?** Error with closest-match suggestions; nothing applied.
- **What happens when an interface and a module have the same name?** Collision error at discovery; nothing applied.
- **What happens when an interface has no providers at all?** Error naming the interface and the modules that require it; nothing applied.
- **What happens when parameter values violate their declared type/constraints?** Plan-time validation error; nothing applied.
- **What happens when the execution records are missing (fresh machine, deleted records)?** The tool never assumes "done": it re-evaluates each step's real state (its declared-satisfied check) and does what is missing; no step is skipped on bookkeeping alone.
- **What happens when a run is interrupted mid-apply?** Completed steps stay journaled; a new run re-attempts only unfinished work (already-satisfied steps skip via their checks).
- **What happens when two runs execute concurrently in the same mode?** The second gets a clear "another run is active in this mode" error; different modes remain independent.
- **What happens when the declared package doesn't exist for the selected backend?** Error including the backend's diagnostic output tail; other recorded results preserved; re-run resumable.
- **What happens when a single-provider interface is required?** It is selected automatically without prompting; the decision (and its "only option" source) is recorded.
- **What happens when a provider declaration is itself missing/invalid?** Discovery error naming the offending file; nothing applied.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST let the user keep all configuration — an entry config, a module tree, static source files, and recorded decisions — as plain text in a single repository that is version-system friendly.
- **FR-002**: System MUST treat a **module** as the unit of declaration: one file (or directory variant) per module, its identity equal to its path; names are normalized so typos/rename mismatches are errors, not surprises.
- **FR-003**: System MUST support **partial application** of configuration: by privilege mode, by selecting a named profile, and by explicitly naming a subset of modules at run time (with their dependency closures included automatically).
- **FR-004**: System MUST let modules declare requirement **dependencies** with automatic, acyclic resolution applied in dependency order; cycles fail at plan time with the chain displayed.
- **FR-005**: System MUST support **interface modules** that carry no content of their own but are required by others, and **provider modules** that satisfy named interfaces; interface/module name collisions and provider-less interfaces are errors.
- **FR-006**: System MUST resolve providers by documented precedence — explicit per-run override → the run's profile pinning → entry-level pinning → the recorded decision → automatic selection if exactly one provider exists → interactive prompt — where automation contexts (non-interactive flag/no terminal) fail on ambiguity with the candidate list instead of guessing silently.
- **FR-007**: System MUST persist resolved provider decisions in a repository-local, plain-text, versionable decisions file (regenerated by the tool), noting each decision's source; later runs never re-ask, and the user can explicitly force reselection.
- **FR-008**: System MUST produce a **deterministic plan**: same configuration + same machine-recorded state must yield the same plan, in a documented, stable order (dependency topological with stable tie-breaking; fixed step-class ordering within a module), so previews are reproducible and reviewable.
- **FR-009**: Every mutating run MUST have an available **preview** showing: selected profile/mode/modules, resolved providers and their origins, every planned step grouped by module with a human-readable description (file destinations, package names, service targets), and any advisories — while performing zero changes.
- **FR-010**: System MUST be **idempotent**: applying an already-satisfied configuration is a no-op; each step class has a real "is the target already in state?" check, applied independently of recorded bookkeeping.
- **FR-011**: System MUST record an **execution fingerprint** per module (its own texts + resolved parameters + resolved providers + selected backend identity + tool/engine version entitlements) and skip re-applying modules whose fingerprint is unchanged and whose recorded result is complete; users can explicitly force re-application.
- **FR-012**: System MUST journal every step of every run — timestamps, result (applied/skipped/failed), failure reason, and where meaningful produced-content digests or backend diagnostics — and expose it via a readable journal command; record readability survives tool restarts.
- **FR-013**: System MUST provide **status**: per module per privilege mode, whether it is applied / stale (fingerprint changed) / pending / failed.
- **FR-014**: System MUST provide **two explicit privilege modes** for a run — user and system — where each run executes only steps of its matching privilege, system parts of a plan are never silently executed in user mode but are reported pending with the elevated-pass hint, and no automatic self-elevation occurs.
- **FR-015**: System MUST keep execution records **in two separate stores by privilege mode** (user store for user runs, system store for system runs) so passes progress and skip independently; concurrent runs in the same mode are prevented with a clear message; runs in different modes do not interfere.
- **FR-016**: System MUST refuse to overwrite pre-existing files it never managed, offering explicit adoption; adoption is opt-in per run or per entry and always logged in the journal.
- **FR-017**: System MUST support declarative module **parameters** with declared defaults, allowed values, and descriptions; invalid overrides fail at plan time with the offending module/parameter named.
- **FR-018**: System MUST enforce the **declarative contract** at read time: configuration must only declare intent; attempts to perform mutations during declaration fail with guidance to declare instead (read-only inspection of the machine is permitted so declarations can adapt).
- **FR-019**: System MUST support standard environment tasks declaratively — packages (names), files (link/copy/create from sources or inline content to destinations), environment variables, services (declare manage the enable/disable state and optional runtime state), system users (basic account properties), locale (generation + default), device rules — so the imperative escape hatch is rarely needed; the escape hatch exists and marks NOT declared intent explicitly (visible in plans and journals) with its own checkable idempotency.
- **FR-020**: System MUST support removable/extensible domain backends behind stable namespaces — in v1: package management and service management — installed as separate add-on deliverables, auto-detected or explicitly configured;
- **FR-021**: With a required backend missing, System MUST fail the affected plan clearly (naming the add-on and how to install it) and MUST keep the core fully functional for all steps that need no backend.
- **FR-022**: System MUST allow a repository-local lightweight extension format for small/experimental backends, visibly flagged as user-trust, and a **list extensions** command showing kind, name, origin path, and interface-version status (refusing incompatible versions with both versions printed).
- **FR-023**: System MUST offer the command surface: initialize/scaffold a repository; list modules/profiles/providers/extensions; show the resolved declaration of one module; sync (apply) with mode flag + flags for preview, yes/no questions, adoption, force refresh, provider override/reselect; status; journal. Exit status must distinguish success / apply failure / configuration or plan error / interrupted.
- **FR-024**: System MUST keep execution records outside the configuration repository (per user decision; the repo stays clean for git), and all error messages must name the failing item (module/step/interface) plus a suggested next action.

### Key Entities *(include if feature involves data)*

- **Configuration Repository**: All user-authored text: entry config, module tree, static source files, extension folder, and the generated decisions file; versionable as a whole.
- **Module**: A named, file-backed declaration; attributes: identity (name), description, declared requirements, optional provider membership, parameters, and content sections (packages/files/env/services/users/locale/device rules/imperative actions).
- **Interface**: A required-name concept with no content; satisfied by providers.
- **Provider**: A module that satisfies an interface; carries a priority used for default highlighting when asked.
- **Profile**: A named selection of modules (+ parameters + provider overrides); marked default or not.
- **Step**: The smallest applied unit of work; attributes: parent module, privilege mode, step class, target description, idempotent satisfied-check, and result record.
- **Run**: A single invocation; attributes: mode, profile, selected scope, resolved providers, ordered steps, outcome; parents the journal records.
- **Decision (fingerprint)**: The per-module hash of everything that determines its result; basis of skip/stale handling.
- **Backend add-on**: A pluggable domain engine binding a namespace (e.g., packages, services) to a concrete platform implementation.
- **Recorded decision**: A resolved interface-provider selection, its source of authority, and the time of record; stored in the repository decisions file.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A technically comfortable user goes from zero to a completed first apply (scaffold → declare → preview → apply) in **under 15 minutes** using fewer than 12 tool commands, with the scaffold command generating a working starter repository.
- **SC-002**: Re-applying an unchanged, fully applied configuration completes in **under 2 seconds** with zero mutation; measured on a repository of tens of modules.
- **SC-003**: Two consecutive previews of the same configuration on the same machine produce **byte-identical** plans (determinism testable in automation).
- **SC-004**: After an interrupted run, a re-run resolves every step to either "already satisfied (skipped)" or "applied correctly" with **100%** of recorded steps intact; no step is duplicated as new work or lost.
- **SC-005**: **100%** of mutating run types can be previewed first; there exists no apply path that cannot be previewed.
- **SC-006**: After a provider choice is made, subsequent runs re-prompt **0 times** unless the user explicitly requests reselection; verified across ≥ 10 consecutive automatic runs in tests.
- **SC-007**: Every error path defined in the specification produces a message naming the failing element **and** a suggested action; validated by an error-message test catalogue (target: 0 dead-end errors in the catalogue).
- **SC-008**: The standard task classes enumerated in FR-019 are representable **declaratively** (no escape hatch) in at least the full v1 scenario set; the escape hatch is exercised in tests only for the intentionally imperative example (e.g., a downloader-style action).
- **SC-009**: Concurrent same-mode execution always yields one proceeding run and one clean refusal; verified by a dedicated test; cross-mode runs proceed independently with no shared-record contention.

## Assumptions

- **Configuration surface**: Per the project brief, configuration is authored as scripted, declarative files in the scripting language the brief mandates (Lua-family). This spec deliberately does **not** specify the engine/runtime, storage formats' internals, or delivery mechanics — those are plan/design concerns (see REQUIREMENTS.md and the project constitution).
- **Target platform**: Linux-first (established by the constitution); representative distro package managers validate the backend design (first add-on: pacman-family), with the abstraction kept clean for others.
- **Single machine per run**: A repository trains one machine at a time; multi-host inventories (beyond profiles) are out of scope for v1.
- **Privilege model**: The user pass runs unattended as the invoking user; the system pass is invoked by the user through normal privilege channels (e.g., sudo) — repd never prompts for passwords or elevates itself.
- **Automation**: Non-interactive runs are a supported class (automation machines): ambiguous choices are hard errors, never silent guesses.
- **Versioning/repd patterns**: Package operations address packages by name; version pinning of packages is out of scope for v1.
- **Distribution of modules**: The tool does not fetch modules from the network by itself in v1; users obtain modules via their version-control workflow (or vendor them into the repository).
- **Destructive actions**: v1 never deletes system users and never removes packages not recorded as repd-managed; sizeable "orphan" cleanup is a separate future feature, not this one.
- **Decisions file**: Committable and reviewed like source; it exists to make automation quiet, not to be hand-edited (though it remains valid plain text).

## Dependencies

- Requires the project constitution constraints (declarative-only apply, safety, test-first) as ratified in `.specify/memory/constitution.md`.
- Backend add-on availability (e.g., pacman backend) is a separate deliverable; the core must remain useful without it (FR-021).
