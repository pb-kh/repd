# repd-yay add-on: packages backend for yay (repo + AUR packages, research R17).
yay_addon = shared_module('librepd-yay', 'src/main.cpp',
  include_directories: inc,
  cpp_args: ['-DREPD_ADDON_VERSION="@0@"'.format(addon_version)],
  name_prefix: '',
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: plugin_install_dir,
  # Install just this add-on: meson install -C build --tags yay
  install_tag: 'yay',
)
