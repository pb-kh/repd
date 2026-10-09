# Meson overlay for LuaJIT (research R2). Mirrors src/Makefile:
# minilua (host) -> DynASM -> buildvm (host) -> lj_vm.S + generated headers
# -> static libluajit. Lua 5.2 compat stays off (plain 5.1 semantics).
project('luajit', 'c', version: '2.1', license: 'MIT')

cc = meson.get_compiler('c')
host_cc = meson.get_compiler('c', native: true)
cpu = target_machine.cpu_family()

if cpu == 'x86_64'
  dasm_arch = 'x64'
  dasm_flags = ['-D', 'ENDIAN_LE', '-D', 'P64', '-D', 'JIT', '-D', 'FFI',
                '-D', 'FPU', '-D', 'HFABI', '-D', 'VER=']
elif cpu == 'aarch64'
  dasm_arch = 'arm64'
  dasm_flags = ['-D', 'ENDIAN_LE', '-D', 'P64', '-D', 'JIT', '-D', 'FFI',
                '-D', 'DUALNUM', '-D', 'FPU', '-D', 'HFABI', '-D', 'VER=80']
else
  error('LuaJIT overlay: unsupported CPU family ' + cpu)
endif

arch_args = ['-DLJ_ARCH_HASFPU=1', '-DLJ_ABI_SOFTFP=0']
common_args = arch_args + [
  '-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE_SOURCE', '-U_FORTIFY_SOURCE',
  '-DLUAJIT_UNWIND_EXTERNAL', '-funwind-tables', '-fno-stack-protector',
]
src_inc = include_directories('src')

minilua = executable('minilua', 'src/host/minilua.c',
  dependencies: host_cc.find_library('m', required: false),
  native: true,
  override_options: ['warning_level=0', 'werror=false', 'b_sanitize=none'],
)

luajit_h = custom_target('luajit.h',
  input: ['src/host/genversion.lua', 'src/luajit_rolling.h', '.relver'],
  output: 'luajit.h',
  command: [minilua, '@INPUT0@', '@INPUT1@', '@INPUT2@', '@OUTPUT@'],
)

buildvm_arch_h = custom_target('buildvm_arch.h',
  input: ['dynasm/dynasm.lua', 'src/vm_' + dasm_arch + '.dasc'],
  output: 'buildvm_arch.h',
  command: [minilua, '@INPUT0@'] + dasm_flags + ['-o', '@OUTPUT@', '@INPUT1@'],
)

buildvm = executable('buildvm',
  'src/host/buildvm.c', 'src/host/buildvm_asm.c', 'src/host/buildvm_peobj.c',
  'src/host/buildvm_lib.c', 'src/host/buildvm_fold.c',
  buildvm_arch_h, luajit_h,
  include_directories: [src_inc, include_directories('dynasm')],
  c_args: arch_args,
  native: true,
  override_options: ['warning_level=0', 'werror=false', 'b_sanitize=none'],
)

ljlib_c = files(
  'src/lib_base.c', 'src/lib_math.c', 'src/lib_bit.c', 'src/lib_string.c',
  'src/lib_table.c', 'src/lib_io.c', 'src/lib_os.c', 'src/lib_package.c',
  'src/lib_debug.c', 'src/lib_jit.c', 'src/lib_ffi.c', 'src/lib_buffer.c',
)

lj_vm_s = custom_target('lj_vm.S', output: 'lj_vm.S',
  command: [buildvm, '-m', 'elfasm', '-o', '@OUTPUT@'])
gen_headers = []
foreach m : ['bcdef', 'ffdef', 'libdef', 'recdef']
  gen_headers += custom_target('lj_' + m + '.h', input: ljlib_c,
    output: 'lj_' + m + '.h',
    command: [buildvm, '-m', m, '-o', '@OUTPUT@', '@INPUT@'])
endforeach
gen_headers += custom_target('lj_folddef.h', input: 'src/lj_opt_fold.c',
  output: 'lj_folddef.h',
  command: [buildvm, '-m', 'folddef', '-o', '@OUTPUT@', '@INPUT@'])

ljcore_c = files(
  'src/lj_assert.c', 'src/lj_gc.c', 'src/lj_err.c', 'src/lj_char.c',
  'src/lj_bc.c', 'src/lj_obj.c', 'src/lj_buf.c', 'src/lj_str.c',
  'src/lj_tab.c', 'src/lj_func.c', 'src/lj_udata.c', 'src/lj_meta.c',
  'src/lj_debug.c', 'src/lj_prng.c', 'src/lj_state.c', 'src/lj_dispatch.c',
  'src/lj_vmevent.c', 'src/lj_vmmath.c', 'src/lj_strscan.c', 'src/lj_strfmt.c',
  'src/lj_strfmt_num.c', 'src/lj_serialize.c', 'src/lj_api.c',
  'src/lj_profile.c', 'src/lj_lex.c', 'src/lj_parse.c', 'src/lj_bcread.c',
  'src/lj_bcwrite.c', 'src/lj_load.c', 'src/lj_ir.c', 'src/lj_opt_mem.c',
  'src/lj_opt_fold.c', 'src/lj_opt_narrow.c', 'src/lj_opt_dce.c',
  'src/lj_opt_loop.c', 'src/lj_opt_split.c', 'src/lj_opt_sink.c',
  'src/lj_mcode.c', 'src/lj_snap.c', 'src/lj_record.c', 'src/lj_crecord.c',
  'src/lj_ffrecord.c', 'src/lj_asm.c', 'src/lj_trace.c', 'src/lj_gdbjit.c',
  'src/lj_ctype.c', 'src/lj_cdata.c', 'src/lj_cconv.c', 'src/lj_ccall.c',
  'src/lj_ccallback.c', 'src/lj_carith.c', 'src/lj_clib.c', 'src/lj_cparse.c',
  'src/lj_lib.c', 'src/lj_alloc.c', 'src/lib_aux.c', 'src/lib_init.c',
)

luajit_lib = static_library('luajit',
  ljcore_c, ljlib_c, lj_vm_s, gen_headers, luajit_h,
  include_directories: src_inc,
  c_args: common_args,
  override_options: ['warning_level=0', 'werror=false', 'optimization=2'],
  pic: true,
)

luajit_dep = declare_dependency(
  link_with: luajit_lib,
  include_directories: [src_inc, include_directories('.')],
  sources: luajit_h,
  dependencies: [cc.find_library('m', required: false),
                 cc.find_library('dl', required: false)],
)
