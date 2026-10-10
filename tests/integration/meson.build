# Sandboxed end-to-end tests (research R18), suite `integration`. They need the
# REPRO_SYSROOT / REPRO_TEST_ASSUME_ROOT hooks, so they only exist in
# -Dtest_hooks=true builds.
if not test_hooks
  subdir_done()
endif

python = find_program('python3')

# Native test plugins for the loader (plugin-abi 1, and a stale ABI 0).
fake_native = shared_module('librepro-fakenative', '../fixtures/plugins/native/fake_native.c',
  include_directories: inc, name_prefix: '', override_options: ['c_std=c11'])
old_abi = shared_module('librepro-oldabi', '../fixtures/plugins/native/old_abi.c',
  include_directories: inc, name_prefix: '', override_options: ['c_std=c11'])

integration_tests = [
  'test_us1_declare',
  'test_us1_adoption',
  'test_us1_drift',
  'test_perf',
  'test_determinism',
  'test_us2_compose',
  'test_us2_override',
  'test_us2_cycles',
  'test_us2_noninteractive',
  'test_providers',
  'test_us3_privilege',
  'test_us3_noprivilege',
  'test_concurrency',
  'test_us4_profiles',
  'test_us4_default',
  'test_us5_no_backends',
  'test_us5_missing_backend',
  'test_us5_backends',
  'test_package_batches',
  'test_us5_repo_local',
  'test_resume',
  'test_errors',
  'test_env_targets',
  'test_openrc_backend',
  'test_files_dirs',
  'test_yay_backend',
  'test_flatpak_backend',
  'test_optional_integrations',
  'test_pacman_backend',
  'test_params_proxies',
]

# First-party add-ons exercised against stub tools (absent with -Daddons=disabled).
addon_deps = []
addon_env = {}
if is_variable('openrc_addon')
  addon_deps += openrc_addon
  addon_env += {'REPRO_OPENRC_PLUGIN': openrc_addon.full_path()}
endif
if is_variable('yay_addon')
  addon_deps += yay_addon
  addon_env += {'REPRO_YAY_PLUGIN': yay_addon.full_path()}
endif
if is_variable('pacman_addon')
  addon_deps += pacman_addon
  addon_env += {'REPRO_PACMAN_PLUGIN': pacman_addon.full_path()}
endif
if is_variable('flatpak_addon')
  addon_deps += flatpak_addon
  addon_env += {'REPRO_FLATPAK_PLUGIN': flatpak_addon.full_path()}
endif

foreach name : integration_tests
  test(name, python,
    args: [files(name + '.py')],
    suite: 'integration',
    timeout: 300,
    depends: [repro_exe, fake_native, old_abi] + addon_deps,
    env: {
      'REPRO_BIN': repro_exe.full_path(),
      'REPRO_SOURCE_ROOT': meson.project_source_root(),
      'REPRO_NATIVE_PLUGIN': fake_native.full_path(),
      'REPRO_OLD_ABI_PLUGIN': old_abi.full_path(),
      'REPRO_PLUGIN_DIR': plugin_dir,
      'PYTHONDONTWRITEBYTECODE': '1',
    } + addon_env,
  )
endforeach
