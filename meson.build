project('repd', 'cpp', 'c',
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

repd_version = meson.project_version()
lua_api_version = 1
plugin_abi_version = 1
# Must match subprojects/luajit.wrap (recorded in `repd --version`).
luajit_commit = 'c6ffc141a876'
# Where repd looks for (and add-ons install) librepd-<name>.so plugins.
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

repd_args = [
  '-DREPD_VERSION="@0@"'.format(repd_version),
  '-DREPD_LUA_API_VERSION=@0@'.format(lua_api_version),
  '-DREPD_TEST_HOOKS=@0@'.format(test_hooks ? 1 : 0),
  '-DREPD_PLUGIN_ABI_VERSION=@0@'.format(plugin_abi_version),
  '-DREPD_PLUGIN_DIR="@0@"'.format(plugin_dir),
  '-DREPD_LUAJIT_COMMIT="@0@"'.format(luajit_commit),
]

inc = include_directories('src', 'include')

# Every embedded Lua file, so edits trigger a rebuild of the #embed units.
lua_sources = files(
  'src/lua/repd/init.lua',
  'src/lua/repd/err.lua',
  'src/lua/repd/util.lua',
  'src/lua/repd/canon.lua',
  'src/lua/repd/sandbox.lua',
  'src/lua/repd/builders.lua',
  'src/lua/repd/params.lua',
  'src/lua/repd/merge.lua',
  'src/lua/repd/lock.lua',
  'src/lua/repd/backend.lua',
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

repd_core = static_library('repdcore', core_sources,
  include_directories: inc,
  cpp_args: repd_args,
  dependencies: core_deps,
  extra_files: lua_sources,
)
repd_core_dep = declare_dependency(
  link_with: repd_core,
  include_directories: inc,
  compile_args: repd_args,
  dependencies: core_deps,
)

# Single self-contained binary (constitution C1): only libc/libm/libdl at runtime.
repd_exe = executable('repd', 'src/main.cpp',
  dependencies: [repd_core_dep, cli11_dep],
  link_args: cpp.get_supported_link_arguments('-static-libstdc++', '-static-libgcc', '-Wl,--gc-sections'),
  install: true,
)

lua_test_exe = executable('repd-lua-test',
  'src/lua_test_main.cpp', 'src/eval/embedded_luaunit.cpp',
  dependencies: repd_core_dep,
  extra_files: files('src/lua/vendor/luaunit.lua'),
)

install_headers('include/repd/plugin.h', subdir: 'repd')

if get_option('addons').allowed()
  subdir('addons')
endif

subdir('tests')
