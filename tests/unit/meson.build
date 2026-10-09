doctest_dep = dependency('doctest', include_type: 'system')

# One doctest executable per component (research R18), suite `unit`.
unit_tests = {
  'sha256': files('test_sha256.cpp'),
  'platform': files('test_platform.cpp'),
  'state': files('test_state.cpp'),
  'eval': files('test_eval.cpp'),
  'plan': files('test_plan.cpp'),
  'resolver': files('test_resolver.cpp'),
  'apply': files('test_apply.cpp'),
  'params': files('test_params.cpp'),
}

# doctest's macros expand __COUNTER__ and its header includes <ciso646>; newer
# Clang diagnoses both in *our* translation units. Silence that for tests only.
doctest_args = cpp.get_supported_arguments('-Wno-c2y-extensions', '-Wno-#warnings')

foreach name, srcs : unit_tests
  exe = executable('test-' + name, srcs, files('doctest_main.cpp'),
    dependencies: [repro_core_dep, doctest_dep],
    cpp_args: doctest_args,
  )
  test(name, exe, suite: 'unit')
endforeach
