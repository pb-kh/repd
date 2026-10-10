# Contract: `repro` command-line interface (v1)

Stable user-facing surface (FR-023). Changes after release follow the
constitution's versioning policy.

## Global options

| Option | Meaning |
|---|---|
| `--config PATH` | Config repository root (overrides `REPRO_CONFIG`; default `${XDG_CONFIG_HOME:-~/.config}/repro` of the target user — `$SUDO_USER` under sudo) |
| `--no-color` | Disable color (also `NO_COLOR` env; color only on TTY otherwise) |
| `-h, --help`, `--version` | `--version` prints `repro <semver> (lua-api <N>, plugin-abi <N>, luajit <commit>, sqlite <ver>)` |

Mode flags where accepted: `-u/--user` (default for read-only commands) and
`-s/--system`. `sync` **requires** exactly one of them.

## Commands

```
repro init [--dir PATH] [--force]
repro list modules|interfaces|profiles|providers|plugins [--profile NAME]
repro show MODULE [--profile NAME]
repro sync (-u|-s) [MODULE ...] [--profile NAME] [--dry-run] [--yes]
           [--adopt] [--refresh MODULE ...] [--provider IFACE=MODULE ...]
           [--reselect IFACE ...] [--option KEY=VALUE ...] [--upgrade]
repro status [-u|-s] [--profile NAME]
repro journal [-u|-s] [--last N] [--run ID]
repro plugins list
```

(`repro plugins list` ≡ `repro list plugins`.)

### `init`
Creates, in `--dir` (default: config path), `repro.lua` (one default profile
`main` using `base`), `modules/base/init.lua` (an example `env` + `files`
entry) with its source `modules/base/files/example.conf`, `plugins/lua/`, and
`.gitignore`. Refuses if `repro.lua` exists unless `--force` (never deletes
other files). Exit 0/2.

### `list`
One item per line, sorted. `modules`: `name  notes  description`, notes being
`provides IFACE`, `integrates A + B`, `optional for M`, `auto_sync = false`
(`; `-separated). `profiles` shows `name+` and `name (auto_sync = false)` entries.
`providers`: per interface, its providers ordered by (priority, name), marking
the currently resolved one and its source. `plugins`: see `plugins list`.

### `show MODULE`
Prints the module's resolved declaration (params applied, `with_params`
merged, providers of its requires resolved) using the canonical pretty
format, then its planned step ids. Read-only; never prompts (unresolved
interface → shown as `<unresolved: candidates …>`).

### `sync`
Pipeline: discover → eval → resolve → plan → preview or apply.
- `--dry-run`: print the plan; write nothing (no DB, no lock, no files). May
  prompt for providers on a TTY unless `--yes`.
- `--yes`: non-interactive; never prompts; ambiguity is an error.
  Non-TTY stdin/stderr implies `--yes` semantics for prompts.
- `--adopt`: allow taking over existing unmanaged destinations in this run (backed up).
- `--refresh M`: force action re-check and post-steps for module M.
- `--provider I=M`: precedence level 1 override; recorded in lock with source `cli`.
- `--reselect I`: ignore the lock entry for I (prompt/auto again).
- `--upgrade`: plan one `upgrade` step per packages backend in use (plus the
  default backend), ahead of that backend's installs and in its pass (step id
  `<first module using it>/upgrade/<backend>`). Installs never refresh package
  databases on their own (a refresh without an upgrade is a partial upgrade);
  backends without an upgrade operation are skipped with a warning. With
  MODULE arguments the upgrade is targeted: one `upgrade` step per backend
  holding the packages the named modules (and `+` extras, not their requires)
  declare, filtered to installed ones by the check (none installed → nothing
  to do); it uses `upgrade_packages`. A backend without it, or whose
  `can_upgrade_packages` refuses (pacman unless `pacman.partial=allow`), is
  skipped with `warning: --upgrade: …; skipped <packages>` and the reason's hint.
- MODULE arguments (and profile `use` entries) may end in `+`: the module plus
  its optional requires. An existing module named `x+` wins.
- The plan lists `optional, not synced` modules (with who offers them) and the
  `integrations` touching the run (`active`, `inactive, needs …`, or held by
  `auto_sync = false`).
- `--option K=V` (repeatable; also on `status` and `show`): overrides
  `rp.option(K, V)` from repro.lua for this run. K is a core option
  (`packages.default_backend`, `services.backend`, `env.targets` as a comma list) or
  one an installed add-on declares (e.g. `yay.review`); an unknown key or a
  value outside the declared ones is `E_CONFIG` (with a did-you-mean), a flag
  without `=` is `E_USAGE`.
- `-s` without effective root → exit 2: `repro sync -s needs root; run: sudo repro sync -s`.
- Without `--dry-run`, the plan is printed first, then applied; there is no
  confirmation prompt in v1 (preview is the explicit `--dry-run`).

### `status`
Table per module in the selected scope: `MODULE  STATUS  LAST-RUN` for the
chosen mode (default `-u`); STATUS is `applied|stale|pending|failed` and
LAST-RUN is `#<run id> <UTC time>` or `-`. Also prints one line for the other
mode: `system: N pending (M modules) — run: sudo repro sync -s` if its store is
readable, else `system: unavailable (run: sudo repro status -s)`.

### `journal`
Most recent runs first (`--last N`, default 5) or a single run (`--run ID`):
run header (id, mode, profile, start/finish, outcome, counts) and its steps in
order (`seq  status  step_id  duration  [error]`, `[imperative]` marked,
adopted entries marked `(adopted, backup: PATH)`). A non-dry-run `sync`
whose plan fails is journaled with outcome `plan-error` and no steps.

## Preview / plan output format (byte-stable, SC-003)

```
repro plan — mode: user — profile: laptop
providers:
  dev/ide = dev/ide/vim (lock)
backends:
  packages = pacman 1.0.0 (system, auto)
modules (4):
  base                 up to date
  dev/git              1 change
    + files   ~/.gitconfig  link -> modules/dev/git/files/gitconfig
  dev/ide/vim          2 changes, 1 for system pass
    + env     EDITOR=nvim  (~/.config/fish/conf.d/repro-dev--ide--vim.fish)
    ~ files   ~/.config/nvim/init.lua  update (content differs)
    · packages install neovim, ripgrep  [system pass]
  dev/rust             1 change
    + action  install rustup  [imperative]
summary: 4 changes in this pass, 1 pending for system pass (run: sudo repro sync -s)
```

Rules:
- Markers: `+` create/install/enable, `~` update/restore drift, `!` blocked,
  `·` other-mode (pending, never executed in this pass).
- Modules in topo order (lexicographic tie-break); steps in class order then key order.
- `~` is rendered literally for paths under the target home; no timestamps,
  PIDs, run ids, or durations appear in the plan section.
- Blocked steps print the reason and the remedy:
  `! files ~/.bashrc  exists and is not managed by repro — re-run with --adopt or set adopt = true`.

Apply then appends result lines `ok|skip|FAIL  step_id` and a final summary:
`done: N applied[ (K adopted, backups kept)][; P pending for <other> pass (run: …)]`,
or `up to date` when nothing ran. Post-steps (`<module>/post/udev-reload`,
`<module>/post/locale-gen`) are listed with class `post` and are not counted
as changes.
On failure the **last line** is `failed: <step_id>: <reason>` (FR-C-4).

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success (including "up to date" and dry-run) |
| 1 | Apply failure (a step failed or was blocked by the adoption guard) |
| 2 | Configuration, plan, or usage error (incl. cycle, ambiguity under `--yes`, missing backend, no root for `-s`, concurrent run) |
| 130 | Interrupted (SIGINT/SIGTERM) |

## Error message contract (SC-007)

Every error is printed to stderr as:

```
error[E_CODE]: <what failed, naming module/step/interface/path>
  hint: <one suggested next action>
```

Catalogue (all covered by `tests/integration/test_errors.py`):
`E_USAGE, E_CONFIG, E_NAME, E_UNKNOWN (with "did you mean"), E_COLLISION,
E_CYCLE, E_NO_PROVIDER, E_AMBIGUOUS, E_PROVIDER, E_PARAM, E_CONFLICT (env/locale),
E_DECLARATIVE, E_NO_BACKEND, E_PLUGIN_ABI, E_PRIVILEGE, E_BUSY, E_BLOCKED,
E_STEP, E_SCHEMA`.

### Additional codes

Also emitted outside the catalogue: `E_INTERRUPTED` (exit 130, SIGINT/SIGTERM)
and `E_IO` (exit 2, unreadable/unwritable paths). `E_PLUGIN_ABI` is fail-soft:
the message is printed, other plugins keep working, and the command's exit
code is unaffected.

## Environment variables

| Var | Effect |
|---|---|
| `REPRO_CONFIG` | Config repo path |
| `NO_COLOR` | Disable color |
| `XDG_CONFIG_HOME`, `XDG_STATE_HOME` | Standard locations |
| `REPRO_SYSROOT`, `REPRO_TEST_ASSUME_ROOT` | **Test builds only** (`-Dtest_hooks=true`); ignored otherwise |
