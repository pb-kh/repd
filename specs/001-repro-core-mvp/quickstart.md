# Quickstart: build, test, and validate repro core MVP

Validates the feature end-to-end against the spec's user stories. Everything
here runs **sandboxed** — never against the live system (Constitution IV).

## 1. Prerequisites

- GCC ≥ 15 or Clang ≥ 19, Meson ≥ 1.4, Ninja, Python 3 (already required by Meson).
- One-time, with network: `meson subprojects download` (fills
  `subprojects/packagecache/`; hashes are verified against the wraps).

## 2. Build and test (hermetic)

```sh
meson setup build --wrap-mode=nodownload -Dtest_hooks=true -Dwerror=true
meson compile -C build
meson test -C build                      # all suites
meson test -C build --suite unit         # C++ doctest
meson test -C build --suite lua          # repro-lua-test + luaunit
meson test -C build --suite integration  # sandboxed end-to-end
```

Release build (test hooks compiled out):

```sh
meson setup build-rel --wrap-mode=nodownload --buildtype=release -Dtest_hooks=false
```

Formatting gate: `ninja -C build clang-format-check`.

## 3. Manual sandbox for scenario walkthroughs

```sh
export SANDBOX=$(mktemp -d)
export HOME=$SANDBOX/home XDG_CONFIG_HOME=$SANDBOX/home/.config XDG_STATE_HOME=$SANDBOX/home/.local/state
export REPRO_SYSROOT=$SANDBOX/root            # test_hooks builds only
export PATH=$PWD/tests/integration/stubs:$PATH # logging stubs for locale-gen/udevadm/useradd
mkdir -p $HOME $REPRO_SYSROOT
R=$PWD/build/repro
```

### US1 — declare once, apply safely

```sh
$R init                                  # scaffold ~/.config/repro
cp tests/fixtures/plugins/fake-packages.lua ~/.config/repro/plugins/lua/
$R sync -u --dry-run                     # plan shown; nothing changed
find $HOME -newer ~/.config/repro/repro.lua -not -path '*/repro/*'   # → empty
$R sync -u                               # applies; summary lists every change
$R sync -u                               # "up to date", 0 changes, < 2 s
echo drift >> ~/.config/example.conf; $R sync -u   # "~ files … restore drift"
echo mine > ~/.bashrc  # then declare ~/.bashrc in base.lua
$R sync -u; echo $?                      # 1, E_BLOCKED with --adopt hint
```

### US2 — dependencies and providers

Use fixture `tests/fixtures/repos/providers/` (interface `dev/ide`, providers
`dev/ide/vim` (priority 10) and `dev/ide/emacs` (20), requirer `dev/c++`).

```sh
$R --config tests/fixtures/repos/providers sync -u dev/c++ --dry-run --yes; echo $?   # 2, E_AMBIGUOUS lists both
$R --config … sync -u dev/c++            # prompt once (default vim); lock written
$R --config … sync -u dev/c++            # no prompt; up to date
$R --config … sync -u dev/c++ --provider dev/ide=dev/ide/emacs   # override; lock source=cli
```

### US3 — two passes

```sh
$R --config tests/fixtures/repos/mixed sync -u    # user steps applied; "N pending for system pass"
$R --config … sync -s; echo $?                    # 2, E_PRIVILEGE (hint: sudo repro sync -s)
REPRO_TEST_ASSUME_ROOT=1 $R --config … sync -s    # system steps → $REPRO_SYSROOT/etc/…, state in $REPRO_SYSROOT/var/lib/repro
$R … sync -u; REPRO_TEST_ASSUME_ROOT=1 $R … sync -s   # both "up to date"
```

### US4 — profiles

```sh
$R --config tests/fixtures/repos/profiles list profiles
$R --config … sync -u --profile server --dry-run   # only server modules
```

### US5 — backends

```sh
$R --config tests/fixtures/repos/files-only sync -u   # works with zero plugins
$R --config tests/fixtures/repos/needs-pkgs sync -u --dry-run; echo $?   # 2, E_NO_BACKEND names repro-pacman
cp -r tests/fixtures/repos/needs-pkgs $SANDBOX/np && cp tests/fixtures/plugins/fake-*.lua $SANDBOX/np/plugins/lua/
$R --config $SANDBOX/np plugins list   # repo-local fakes flagged reduced trust
```

## 4. Success-criteria checks (automated in `--suite integration`)

| SC | Test |
|---|---|
| SC-002 | `test_perf.py`: 40-module fixture, second `sync -u` < 2 s (median of 5) |
| SC-003 | `test_determinism.py`: two dry-runs byte-identical |
| SC-004 | `test_resume.py`: SIGINT injected after step k via fake backend; re-run completes, journal shows no duplicates |
| SC-006 | `test_providers.py`: 10 consecutive `--yes` runs after one prompt, 0 prompts |
| SC-007 | `test_errors.py`: error catalogue, each names element + hint |
| SC-009 | `test_concurrency.py`: two same-mode syncs → one exit 2 E_BUSY; cross-mode both proceed |
