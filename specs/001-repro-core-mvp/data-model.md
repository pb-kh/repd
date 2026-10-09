# Data Model: repro core MVP

**Feature**: `001-repro-core-mvp` | **Date**: 2026-10-09
Decisions referenced as R# are in [research.md](./research.md).

Three layers of data exist:

1. **Declaration model** — produced by Lua evaluation of the config repository
   (in-memory Lua tables, handed to C++ as a plain data tree).
2. **Plan model** — produced by resolution + planning (C++ structs), the input
   to preview and apply.
3. **Persistent state** — per-mode SQLite store + repo-local `repro.lock.lua`.

---

## 1. Declaration model

### ConfigRepository

| Field | Type | Rule |
|---|---|---|
| root | path | `--config` › `REPRO_CONFIG` › `${XDG_CONFIG_HOME:-~/.config}/repro` (target user's, R12) |
| entry | file | `repro.lua` — MUST exist |
| lock | file | `repro.lock.lua` — optional, generated |
| modules_dir | dir | `modules/` — optional (empty config is valid) |
| lua_plugins_dir | dir | `plugins/lua/` — repo-local backends (reduced trust) |

### RootConfig (from `repro.lua`)

| Field | Type | Rule |
|---|---|---|
| options | map<string, scalar> | known keys only: `packages.backend` (default backend for unprefixed packages; a backend name, no `"auto"`), `services.backend` (`"auto"` or backend name), `env.targets`. Unknown key → E_CONFIG with closest match. |
| providers | map<iface, module> | repo-wide pins (`rp.pin(iface, module)`), precedence level 3 |
| profiles | list<Profile> | names unique; ≤ 1 with `default = true` |

### Profile

| Field | Type | Rule |
|---|---|---|
| name | string | `[a-z0-9_-]+`, unique |
| description | string? | |
| default | bool | at most one profile true |
| use | list<UseEntry> | non-empty; UseEntry = `"name"` or `{ "name", params = {…} }` (`name =` key also accepted) |
| providers | map<iface, module> | precedence level 2 |

### Module

| Field | Type | Rule |
|---|---|---|
| name | ModuleName | = path under `modules/` minus `.lua` / `/init.lua`; if given explicitly MUST equal path |
| kind | `module` \| `interface` | from builder used (`rp.module` / `rp.interface`) |
| description | string? | interfaces: required |
| provides | ModuleName? | MUST name an existing interface |
| priority | int | default 100; lower = preferred (prompt default) |
| requires | list<ModuleName \| { ModuleName, optional = bool }> | each resolves to module or interface, else E_UNKNOWN with suggestions; optional ones are listed, not followed |
| integrates | list<ModuleName>? | non-empty; makes the module an integration (see Scope selection) |
| auto_sync | bool | default true; false: an integration joins only when named |
| params | map<string, Param> | |
| with_params | function(p, sys)? | EVAL-phase; returns content fragment (R11) |
| packages | list<string> | non-empty strings, de-duplicated |
| files | list<FileEntry> | |
| env | map<VarName, string> | user scope |
| system_env | map<VarName, string> | system scope (R12; new vs REQUIREMENTS) |
| services | list<ServiceEntry> | |
| users | list<UserEntry> | system only |
| locale | LocaleEntry? | system only |
| udev | list<UdevEntry> | system only |
| actions | list<Action> | |
| source_path | path | (host-set) absolute path of the module file |

- **ModuleName**: `^[a-z0-9_+-]+(/[a-z0-9_+-]+)*$`.
- **VarName**: `^[A-Za-z_][A-Za-z0-9_]*$`.
- Unknown fields → E_CONFIG naming field + module + closest valid field (typo guard).
- Interfaces MUST NOT have any content field, `provides`, `params`, or `requires`.
- Same name defined as both `modules/x.lua` and `modules/x/init.lua` → E_CONFIG.

### Param

| Field | Type | Rule |
|---|---|---|
| type | `string`\|`number`\|`boolean`\|`list`\|`map` | inferred from `default` if omitted |
| default | value | MUST satisfy type/`one_of` |
| one_of | list<scalar>? | |
| description | string? | |

Validation of overrides happens at plan time → E_PARAM naming module, param, value, allowed values.

### FileEntry

| Field | Type | Rule |
|---|---|---|
| dest | string | absolute or `~/…`; normalized; no `..` after normalization |
| src | string? | exactly one of `src` / `content`; relative to the module's `files/` dir (`modules/<name>/files/`), MUST stay inside it; only directory modules (`<name>/init.lua`) may use `src`; may name a directory: `copy` expands to one managed file per file under it (dest/rel, executable bit kept unless `mode` is set), `link` symlinks the whole directory |
| content | string? | |
| action | `link`\|`copy`\|`create` | default `link` if `src`, `create` if `content`; `link`+`content` → error |
| mode | octal string? | e.g. `"0600"`; not allowed with `link` |
| scope | `user`\|`system`? | see R12 |
| adopt | bool | default false (FR-016) |

### ServiceEntry

`unit` (string, required), `scope` (`system` default \| `user`),
`state` (`enabled` \| `disabled`, required), `active` (bool?, optional runtime state).

### UserEntry (system)

`name` (required, `^[a-z_][a-z0-9_-]*$`), `shell` (path?), `groups` (list?, additive only),
`state` (only `"present"` in v1). Never deletes users or removes group membership.

### LocaleEntry (system)

`locales` (list<string>), `default` (string?, MUST be in the union of declared locales).

### UdevEntry (system)

`name` (`^[0-9A-Za-z._-]+\.rules$`), exactly one of `src` / `content`.

### Action (imperative escape hatch)

| Field | Type | Rule |
|---|---|---|
| name | string | unique within module |
| mode | `user`\|`system` | required |
| check | function(ctx) → bool | required; runs at APPLY/PLAN-check time, never EVAL |
| run | function(ctx) | required; APPLY only |

Rendered in plans and journals with an explicit `[imperative]` marker (FR-019).

### Backend (pure-Lua or `.so`)

| Field | Type | Rule |
|---|---|---|
| namespace | `packages`\|`services` | v1 namespaces |
| name | string | `[a-z0-9_-]+`, unique per namespace |
| version | string | reported by plugin (`.so`) or `"lua"` + file digest prefix |
| origin | `system`\|`user`\|`repo` | from load path; `repo` = reduced trust (flagged) |
| privilege | `user`\|`system` | packages: which mode owns its steps |
| abi | int | `.so` only; MUST equal host `REPRO_PLUGIN_ABI` |
| ops | set | packages: `probe, installed, install, remove, refresh`; services: `probe, state, enable, disable, start, stop` |

---

## 2. Plan model

### Run (in-memory and persisted)

| Field | Type |
|---|---|
| mode | `user` \| `system` |
| profile | string? (null when only explicit modules + no profiles) |
| requested | list<ModuleName> (CLI positional) |
| selected | ordered list<ModuleName> — closure, topo order |
| named | list<ModuleName> — roots and `+` extras (targets of `--upgrade MODULE…`) |
| optional | map<ModuleName, list> — optional requires not selected |
| integrations | list<{name, active, held, missing}> |
| providers | map<iface, ResolvedProvider> |
| backends | map<namespace, Backend id> |
| steps | ordered list<Step> |
| dry_run, yes, adopt | bool |
| refresh | set<ModuleName> |

### Scope selection

1. Roots = CLI module args if given; else selected profile's `use` (minus
   `{ m, auto_sync = false }` override entries); else (no profiles declared)
   error E_USAGE "no modules selected". A root `m+` (no module named `m+`)
   adds `m`'s optional requires.
2. Closure over non-optional `requires` and `integrates`; each interface
   replaced by its ResolvedProvider; providers' own `requires` join
   transitively. Optional requires add no edge.
2a. Integrations: repeat until nothing changes — a module with `integrates`,
   not yet selected, whose effective `auto_sync` (profile override, else the
   module's) is true and whose every `integrates` entry is selected (an
   interface counts when its already-resolved provider is) joins, with its
   closure. Integrations with some entries selected are reported: active,
   inactive (missing entries), or held (auto_sync false).
3. A provider used directly as a root satisfies its interface (FR-I-5) → no prompt.
4. Cycle detection (DFS, colors) → E_CYCLE with chain `a → b → c → a`.
5. Order: Kahn topological sort, ties broken by lexicographic module name (byte order).

### ResolvedProvider

`interface`, `provider`, `source` ∈ {`cli`, `profile`, `pin`, `lock`, `only-option`, `prompt`}, `decided_at` (UTC ISO-8601, persisted only).

Resolution state machine per interface:

```
           ┌─ cli override ────────────────┐
           ├─ profile.providers ───────────┤
 unresolved├─ repo pin ────────────────────┼──▶ resolved(source)
           ├─ lock (unless --reselect) ────┤
           ├─ exactly 1 provider ──────────┤
           ├─ TTY & !--yes → prompt ───────┘
           └─ otherwise ──────────────────────▶ E_AMBIGUOUS (exit 2, candidates listed)
 0 providers ─────────────────────────────────▶ E_NO_PROVIDER (names requirers)
```

Any override naming a module that does not provide the interface → E_PROVIDER.

### Step

| Field | Type | Rule |
|---|---|---|
| id | string | deterministic: `<module>/<class>/<key>`; key = dest path, unit, user name, `install`, action name, … |
| module | ModuleName | |
| class | enum | fixed order within module: `packages, files, env, services, users, locale, udev, action` |
| privilege | `user`\|`system` | R12 |
| description | string | human text for preview |
| desired | data | class-specific desired state |
| check_result | `satisfied`\|`needs-change`\|`blocked`\|`skipped-fp`\|`other-mode` | computed at plan time |
| diff_summary | string? | e.g. "create", "update (content differs)", "install: clang, cmake" |
| imperative | bool | true for actions |

Post-steps (`udev/reload`, `locale/generate`) are synthesized once per run,
placed after the last step of their class in topo order, and only executed if a
contributing step changed.

### Step result lifecycle (apply)

```
planned ──recheck──▶ satisfied ──▶ [skip]
   │                     
   └─needs-change──▶ running ──▶ ok
                        │──────▶ fail ──▶ run stops (exit 1)
                        └──────▶ interrupted (exit 130)
blocked (adoption guard) ──▶ run stops before the module's first mutation (exit 1)
other-mode ──▶ counted as pending for the other pass (never executed)
```

### Module status (per mode; FR-013)

| Status | Condition |
|---|---|
| applied | last complete run's fingerprint == current fingerprint and all its steps `ok`/`skip` |
| stale | recorded fingerprint ≠ current fingerprint |
| pending | no record, or unsatisfied steps exist for this mode |
| failed | latest run touching the module ended with a failed/interrupted step of it |

Precedence when several hold: failed > stale > pending > applied.

### Fingerprint

SHA-256 over `canon.encode` of the record defined in research R10. Stored per
(mode, module). Changing profile params/providers for a module changes its
fingerprint (FR-P-4).

---

## 3. Persistent state

### 3.1 Per-mode SQLite store (FR-015, FR-024)

Location: user `${XDG_STATE_HOME:-~/.local/state}/repro/repro.db`; system
`/var/lib/repro/repro.db` (0600 root). Lock file `run.lock` beside it. WAL mode.

```sql
CREATE TABLE meta(
  key   TEXT PRIMARY KEY,               -- 'schema_version', 'created_by'
  value TEXT NOT NULL
);

CREATE TABLE runs(
  id          INTEGER PRIMARY KEY,
  started_at  TEXT NOT NULL,            -- UTC ISO-8601
  finished_at TEXT,
  mode        TEXT NOT NULL CHECK (mode IN ('user','system')),
  profile     TEXT,
  argv        TEXT NOT NULL,            -- canon-encoded list
  repro_version TEXT NOT NULL,
  outcome     TEXT CHECK (outcome IN ('ok','failed','interrupted','plan-error')),
  exit_code   INTEGER,
  n_applied INTEGER DEFAULT 0, n_skipped INTEGER DEFAULT 0,
  n_failed  INTEGER DEFAULT 0, n_pending_other INTEGER DEFAULT 0
);

CREATE TABLE run_providers(
  run_id    INTEGER NOT NULL REFERENCES runs(id),
  interface TEXT NOT NULL,
  provider  TEXT NOT NULL,
  source    TEXT NOT NULL,
  PRIMARY KEY (run_id, interface)
);

CREATE TABLE run_modules(
  run_id      INTEGER NOT NULL REFERENCES runs(id),
  module      TEXT NOT NULL,
  fingerprint TEXT NOT NULL,            -- hex sha256
  status      TEXT NOT NULL CHECK (status IN ('ok','failed','interrupted','skipped','blocked')),
  PRIMARY KEY (run_id, module)
);

CREATE TABLE steps(
  run_id      INTEGER NOT NULL REFERENCES runs(id),
  seq         INTEGER NOT NULL,          -- order within run
  module      TEXT NOT NULL,
  step_id     TEXT NOT NULL,
  class       TEXT NOT NULL,
  imperative  INTEGER NOT NULL DEFAULT 0,
  status      TEXT NOT NULL CHECK (status IN ('ok','skip','fail','interrupted','blocked')),
  started_at  TEXT NOT NULL,
  finished_at TEXT,
  error       TEXT,
  detail      TEXT,                      -- canon-encoded: digests, package list, output tail (≤ 4 KiB)
  adopted     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (run_id, seq)
);
CREATE INDEX steps_by_module ON steps(module, run_id);

-- Latest known-good state per module (denormalized for fast status/skip)
CREATE TABLE module_state(
  module      TEXT PRIMARY KEY,
  fingerprint TEXT NOT NULL,
  status      TEXT NOT NULL,             -- applied|failed|pending
  last_run_id INTEGER NOT NULL REFERENCES runs(id),
  updated_at  TEXT NOT NULL
);

-- Ownership ledger for the adoption guard (R14)
CREATE TABLE managed_files(
  path        TEXT PRIMARY KEY,          -- absolute, sysroot-relative in tests
  kind        TEXT NOT NULL CHECK (kind IN ('link','copy','create','env','udev','locale-block')),
  digest      TEXT,                      -- sha256 of content (copy/create/env/udev)
  link_target TEXT,                      -- for link
  module      TEXT NOT NULL,
  updated_at  TEXT NOT NULL
);

-- Packages installed by repro (prerequisite for a future `repro prune`; v1 never removes)
CREATE TABLE managed_packages(
  backend TEXT NOT NULL, name TEXT NOT NULL, module TEXT NOT NULL,
  first_run_id INTEGER NOT NULL REFERENCES runs(id),
  PRIMARY KEY (backend, name)
);
```

Write discipline: one transaction to open the run row; **one transaction per
step** (step row + ledger updates) committed immediately after the step
finishes; one transaction to close the run and update `module_state`.
Migrations: ordered list, applied in a single transaction, `schema_version`
bumped; a DB with a *newer* schema than the binary → refuse with both versions
(no downgrade).

### 3.2 `repro.lock.lua` (repo-local decisions; FR-007)

```lua
-- Generated by repro. Do not edit by hand; use --provider / --reselect.
return {
  version = 1,
  providers = {
    ["dev/ide"] = { provider = "dev/ide/vim", source = "prompt", decided_at = "2026-10-09T12:00:00Z" },
  },
}
```

Rules in [contracts/lockfile.md](./contracts/lockfile.md). Only sources `cli`,
`prompt`, `only-option` produce/refresh lock entries; `profile`/`pin` decisions
are already in versioned config and are not duplicated. Entries for interfaces
no longer required are retained (other profiles may need them).

---

## 4. Entity ↔ requirement traceability

| Entity | Requirements |
|---|---|
| ConfigRepository, RootConfig | FR-001, FR-024 |
| Module, ModuleName | FR-002, FR-017, FR-019 |
| Scope selection | FR-003, FR-004 |
| Interface/Provider, ResolvedProvider, lock | FR-005, FR-006, FR-007 |
| Step, ordering | FR-008, FR-009, FR-010 |
| Fingerprint, module_state | FR-011, FR-013 |
| runs/steps tables | FR-012 |
| privilege, per-mode stores, run.lock | FR-014, FR-015 |
| managed_files | FR-016 |
| Param | FR-017 |
| EVAL phase | FR-018 |
| Backend | FR-020, FR-021, FR-022 |
