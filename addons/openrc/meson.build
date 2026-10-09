# repro-openrc add-on: services backend for OpenRC (research R17).
openrc_addon = shared_module('librepro-openrc', 'src/main.cpp',
  include_directories: inc,
  cpp_args: ['-DREPRO_ADDON_VERSION="@0@"'.format(addon_version)],
  name_prefix: '',
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: plugin_install_dir,
  # Install just this add-on: meson install -C build --tags openrc
  install_tag: 'openrc',
)
