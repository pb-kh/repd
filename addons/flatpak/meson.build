# repro-flatpak add-on: packages backends for flatpak, system-wide (`flatpak`)
# and per-user (`flatpak-user`) installations (research R17).
flatpak_addon = shared_module('librepro-flatpak', 'src/main.cpp',
  include_directories: inc,
  cpp_args: ['-DREPRO_ADDON_VERSION="@0@"'.format(addon_version)],
  name_prefix: '',
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: plugin_install_dir,
  # Install just this add-on: meson install -C build --tags flatpak
  install_tag: 'flatpak',
)
