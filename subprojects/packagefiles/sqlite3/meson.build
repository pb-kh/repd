project('sqlite3', 'c', version: '3.46.1', license: 'blessing')

sqlite3_lib = static_library('sqlite3', 'sqlite3.c',
  c_args: [
    '-DSQLITE_OMIT_LOAD_EXTENSION',
    '-DSQLITE_DEFAULT_FOREIGN_KEYS=1',
    '-DSQLITE_THREADSAFE=0',
    '-DSQLITE_DQS=0',
    '-DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1',
  ],
  override_options: ['warning_level=0', 'werror=false'],
  pic: true,
)

sqlite3_dep = declare_dependency(
  link_with: sqlite3_lib,
  include_directories: include_directories('.'),
)
