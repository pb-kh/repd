# repd-systemd add-on: a separately packaged backend plugin (research R17).
shared_module('librepd-systemd', 'src/main.cpp',
  include_directories: inc,
  cpp_args: ['-DREPD_ADDON_VERSION="@0@"'.format(addon_version)],
  name_prefix: '',
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: plugin_install_dir,
  # Install just this add-on: meson install -C build --tags systemd
  install_tag: 'systemd',
)
