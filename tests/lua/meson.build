# luaunit suites run inside repd-lua-test (research R18), suite `lua`.
lua_tests = [
  'test_canon',
  'test_sandbox',
  'test_params',
  'test_merge',
  'test_lock',
]

foreach name : lua_tests
  test(name, lua_test_exe,
    args: [files(name + '.lua'), '--verbose'],
    suite: 'lua',
    env: {'NO_COLOR': '1'},
  )
endforeach

# Docs registry check and determinism lint take a path argument.
test('test_api_docs', lua_test_exe,
  args: [files('test_api_docs.lua'), meson.project_source_root() / 'docs' / 'lua-api.md'],
  suite: 'lua')
test('test_lint', lua_test_exe,
  args: [files('test_lint.lua'), meson.project_source_root() / 'src' / 'lua' / 'repd'],
  suite: 'lua')
