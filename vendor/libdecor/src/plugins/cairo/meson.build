pangocairo_dep = dependency('pangocairo')
wayland_cursor_dep = dependency('wayland-cursor')

shared_module('decor-cairo',
  sources: [
    'libdecor-cairo.c',
  ],
  include_directories: [
    top_includepath,
    libdecor_includepath,
    plugin_include_path,
  ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    libdecor_dep,
    pangocairo_dep,
    wayland_cursor_dep,
    desktop_settings_dep,
    os_compatibility_dep,
  ],
  link_with: plugin_common,
  install_dir: join_paths(plugindir),
  install: true,
)

if meson.version().version_compare('>=0.58.0')
  devenv.append('LIBDECOR_PLUGIN_DIR', meson.current_build_dir())
endif
