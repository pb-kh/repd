# Contract: Lua configuration API (`rp`, lua-api version 1)

This is the public contract of Constitution Principle II. The normative,
user-facing reference is `docs/lua-api.md` (created during implementation and
kept in sync by a test, research R18). Lua dialect: LuaJIT / Lua 5.1.

## Phases

| Phase | When | Mutations allowed? |
|---|---|---|
| EVAL | loading `repro.lua`, module files, `with_params`, repo Lua plugins' top level | **No** — mutating calls raise `E_DECLARATIVE` |
| CHECK | running step satisfied-checks and action `check(ctx)` | No |
| APPLY | running steps and action `run(ctx)` | Yes, via `ctx` only |

## Environment of config files

Fresh table per file. Available: `rp`, `string`, `table`, `math` (without
`random`, `randomseed`), `ipairs`, `pairs`, `next`, `select`, `type`,
`tostring`, `tonumber`, `error`, `assert`, `pcall`, `unpack`,
`setmetatable`, `getmetatable`, `rawget`, `rawset`, `rawequal`.
Unavailable: `io`, `os`, `require`, `load`, `loadstring`, `loadfile`,
`dofile`, `debug`, `ffi`, `jit`, `package`, `collectgarbage`.
Reading an undefined global raises an error; if it resembles an `rp.*`
symbol (e.g. `module`, `packages`) the message suggests it.

## Root config builders (`repro.lua` only)

```lua
rp.option(key, value)          -- key ∈ {"packages.backend","services.backend"}; value "auto" | backend name
rp.pin(interface, module)      -- repo-wide provider pin (precedence level 3)
rp.profile { name=, description=?, default=?, use={...}, providers=? }
```

Calling a root-only builder from a module file → `E_CONFIG`.

## Module file builders

A module file MUST `return` exactly one of:

```lua
return rp.module { ... }       -- fields: see data-model.md "Module"
return rp.interface { name=?, description= }
```

Helpers usable inside them:

```lua
rp.param { default=, type=?, one_of=?, description=? }
rp.action { name=, mode="user"|"system", check=function(ctx) end, run=function(ctx) end }
```

### Module fields (v1)

`name, description, provides, priority, requires, params, with_params,
packages, files, env, system_env, services, users, locale, udev, actions`.
Any other key → `E_CONFIG` with closest valid name. (REQUIREMENTS' `modules`
sub-assembly field is **not** in v1; use `requires`.)

`with_params = function(p, sys) return fragment end` — `p` is the resolved,
validated param table (read-only); `sys` is `rp.sys`. The fragment may contain
only content fields (`packages, files, env, system_env, services, users,
locale, udev, actions`). Merge: lists concatenated + de-duplicated; maps merged,
conflicting keys → `E_CONFLICT`.

## Read-only query API (EVAL, CHECK, APPLY)

```lua
rp.fs.exists(path) -> bool           -- `~` expanded to target home
rp.fs.read(path) -> string|nil       -- ≤ 1 MiB
rp.fs.stat(path) -> {type=, mode=, size=, uid=, gid=}|nil
rp.fs.list(dir) -> {names...}        -- sorted
rp.sys.hostname() / user() / home() / arch() -> string
rp.sys.os_release() -> {ID=, VERSION_ID=, ...}
rp.packages.installed(names) -> {name=bool}   -- requires a selected packages backend
rp.services.state(unit, scope?) -> {enabled=bool, active=bool}|nil
```

Results of these calls affect the resolved declaration, which is part of the
fingerprint, so adapting to the machine stays deterministic per machine state.

## Mutating facades (APPLY only)

`rp.packages.install/remove/refresh`, `rp.services.enable/disable/start/stop`
exist for the host and backends; calling them in EVAL or CHECK raises:

```
error[E_DECLARATIVE]: rp.packages.install() changes the system while configuration is being read (module dev/c++)
  hint: declare it instead: packages = { "clang" }
```

## Action context `ctx` (frozen for lua-api 1)

```lua
ctx.mode          -- "user" | "system"
ctx.home, ctx.user
ctx.params        -- resolved params of the owning module (read-only)
ctx.providers     -- resolved interface → provider map (read-only)
ctx.fs.exists/read/stat/list   -- as rp.fs
ctx.exec(cmd [, opts]) -> {code=, stdout=, stderr=}   -- APPLY only; /bin/sh -c cmd; output tail journaled
ctx.log(msg)      -- appended to the step's journal detail
```

`ctx.exec` in `check` raises `E_DECLARATIVE` (checks must not mutate; use `ctx.fs`).

## Repo-local backends (`plugins/lua/*.lua`)

```lua
return rp.backend {
  namespace = "packages" | "services",
  name      = "fake",
  privilege = "user" | "system",       -- packages only; default "system"
  ops = {
    probe     = function(ctx) return true end,
    installed = function(ctx, names) return { [name] = bool } end,     -- packages
    install   = function(ctx, names) end,
    remove    = function(ctx, names) end,
    refresh   = function(ctx) end,
    state     = function(ctx, unit, scope) return { enabled=, active= } end, -- services
    enable/disable/start/stop = function(ctx, unit, scope) end,
  },
}
```

`ctx` for backends additionally provides `ctx.mem.fetch(key, default)` /
`ctx.mem.update(key, names, value)` — a JSON-free key/value scratch store kept
in `<state-dir>/lua-backend-mem/<backend>.lua` (canon-encoded), intended for
fake backends in tests. Repo-local backends are flagged `origin = repo
(reduced trust)` in previews and `plugins list`.

## Versioning

`rp.api_version` (integer, currently 1). Additive changes keep the number;
removals/semantic changes bump it and require a constitution-governed
migration note.
