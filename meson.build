project('repro', 'cpp', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 1.4',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
    'force_fallback_for=luajit,sqlite3',
    'buildtype=debugoptimized',
  ],
)

repro_version = meson.project_version()
lua_api_version = 1
plugin_abi_version = 1
# Must match subprojects/luajit.wrap (recorded in `repro --version`).
luajit_commit = 'c6ffc141a876'
# Where repro looks for (and add-ons install) librepro-<name>.so plugins.
plugin_dir = get_option('plugin_dir')

cpp = meson.get_compiler('cpp')

# Third-party dependencies are pinned wraps (research R2, R3); their headers
# are system includes so their warnings never fail our -Werror builds.
luajit_dep = dependency('luajit', include_type: 'system')
sqlite3_dep = dependency('sqlite3', include_type: 'system')
cli11_dep = dependency('cli11', include_type: 'system')
dl_dep = cpp.find_library('dl', required: false)

test_hooks = get_option('test_hooks')
# Per-function sections let the linker drop unused code from the static runtime.
add_project_arguments(cpp.get_supported_arguments('-ffunction-sections', '-fdata-sections'),
  language: 'cpp')

repro_args = [
  '-DREPRO_VERSION="@0@"'.format(repro_version),
  '-DREPRO_LUA_API_VERSION=@0@'.format(lua_api_version),
  '-DREPRO_TEST_HOOKS=@0@'.format(test_hooks ? 1 : 0),
  '-DREPRO_PLUGIN_ABI_VERSION=@0@'.format(plugin_abi_version),
  '-DREPRO_PLUGIN_DIR="@0@"'.format(plugin_dir),
  '-DREPRO_LUAJIT_COMMIT="@0@"'.format(luajit_commit),
]

inc = include_directories('src', 'include')

# Every embedded Lua file, so edits trigger a rebuild of the #embed units.
lua_sources = files(
  'src/lua/repro/init.lua',
  'src/lua/repro/err.lua',
  'src/lua/repro/util.lua',
  'src/lua/repro/canon.lua',
  'src/lua/repro/sandbox.lua',
  'src/lua/repro/builders.lua',
  'src/lua/repro/params.lua',
  'src/lua/repro/merge.lua',
  'src/lua/repro/lock.lua',
  'src/lua/repro/backend.lua',
)

core_sources = files(
  'src/util/sha256.cpp',
  'src/util/errors.cpp',
  'src/util/strings.cpp',
  'src/util/value.cpp',
  'src/platform/fs.cpp',
  'src/platform/proc.cpp',
  'src/platform/users.cpp',
  'src/platform/paths.cpp',
  'src/eval/embedded.cpp',
  'src/eval/bridge.cpp',
  'src/eval/host.cpp',
  'src/state/migrations.cpp',
  'src/state/db.cpp',
  'src/state/ledger.cpp',
  'src/state/run_lock.cpp',
  'src/state/config.cpp',
  'src/eval/declaration.cpp',
  'src/eval/discovery.cpp',
  'src/eval/evaluate.cpp',
  'src/plan/scope.cpp',
  'src/plan/resolver.cpp',
  'src/plan/graph.cpp',
  'src/plan/fingerprint.cpp',
  'src/plan/lock.cpp',
  'src/plan/plan.cpp',
  'src/plan/privilege.cpp',
  'src/plan/builder.cpp',
  'src/plan/env_targets.cpp',
  'src/plugins/backends.cpp',
  'src/plugins/loader.cpp',
  'src/apply/checks.cpp',
  'src/apply/adoption.cpp',
  'src/apply/files.cpp',
  'src/apply/packages.cpp',
  'src/apply/services.cpp',
  'src/apply/users.cpp',
  'src/apply/locale.cpp',
  'src/apply/actions.cpp',
  'src/apply/signals.cpp',
  'src/apply/engine.cpp',
  'src/cli/output.cpp',
  'src/cli/prompt.cpp',
  'src/cli/pipeline.cpp',
  'src/cli/preview.cpp',
  'src/cli/commands.cpp',
)

core_deps = [luajit_dep, sqlite3_dep, dl_dep]

repro_core = static_library('reprocore', core_sources,
  include_directories: inc,
  cpp_args: repro_args,
  dependencies: core_deps,
  extra_files: lua_sources,
)
repro_core_dep = declare_dependency(
  link_with: repro_core,
  include_directories: inc,
  compile_args: repro_args,
  dependencies: core_deps,
)

# Single self-contained binary (constitution C1): only libc/libm/libdl at runtime.
repro_exe = executable('repro', 'src/main.cpp',
  dependencies: [repro_core_dep, cli11_dep],
  link_args: cpp.get_supported_link_arguments('-static-libstdc++', '-static-libgcc', '-Wl,--gc-sections'),
  install: true,
)

lua_test_exe = executable('repro-lua-test',
  'src/lua_test_main.cpp', 'src/eval/embedded_luaunit.cpp',
  dependencies: repro_core_dep,
  extra_files: files('src/lua/vendor/luaunit.lua'),
)

install_headers('include/repro/plugin.h', subdir: 'repro')

if get_option('addons').allowed()
  subdir('addons')
endif

subdir('tests')
