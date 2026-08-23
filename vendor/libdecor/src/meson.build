pkg = import('pkgconfig')

libdecor_includepath = include_directories('.')

libdecor_includes = [
  libdecor_includepath,
  top_includepath,
]

libdecor_sources = [
  'libdecor.c',
  'libdecor-fallback.c',
]

libdecor_headers = [
  'libdecor.h',
]
install_headers(libdecor_headers,
  subdir: '@0@'.format(libdecor_full_name),
)

libdecor_built_sources = []

wayland_scanner = find_program('wayland-scanner')

# Format:
#  - protocol stability
#  - protocol name
#  - optional: protocol version, if unstable
wayland_protocols = [
  ['stable', 'xdg-shell'],
  ['unstable', 'xdg-decoration', '1'],
]

protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
assert(protocols_dir != '', 'Could not get pkgdatadir from wayland-protocols.pc')

foreach p: wayland_protocols
  stability = p.get(0)
  name = p.get(1)

  assert(stability in ['stable', 'unstable'],
         'protocol \'@0@\' must be \'stable\' or \'unstable\''.format(name))

  suffix = stability=='unstable' ? '-unstable-v@0@'.format(p.get(2)) : ''

  output_base = name
  input = join_paths(protocols_dir, stability, name, name+suffix+'.xml')

  libdecor_built_sources += custom_target('@0@ client header'.format(output_base),
    input: input,
    output: '@0@-client-protocol.h'.format(output_base),
    command: [
      wayland_scanner,
      'client-header',
      '@INPUT@', '@OUTPUT@',
    ]
  )
  libdecor_built_sources += custom_target('@0@ source'.format(output_base),
    input: input,
    output: '@0@-protocol.c'.format(output_base),
    command: [
      wayland_scanner,
      'private-code',
      '@INPUT@', '@OUTPUT@',
    ]
  )
endforeach

## desktop settings
desktop_settings = static_library('desktop_settings',
  sources: ['desktop-settings.c'],
  include_directories: [top_includepath],
  dependencies: [dbus_dep],
  gnu_symbol_visibility: 'hidden',
)

desktop_settings_dep = declare_dependency(
  link_with: desktop_settings,
  dependencies: [dbus_dep],
)

## os compatibility
os_compatibility = static_library('os_compatibility',
  sources: ['os-compatibility.c'],
  include_directories: [top_includepath],
  gnu_symbol_visibility: 'hidden',
)

os_compatibility_dep = declare_dependency(
  link_with: os_compatibility,
)

## core decor library
libdecor = shared_library(libdecor_name,
  sources: [
    libdecor_sources,
    libdecor_built_sources,
  ],
  soversion: libdecor_soversion,
  version: libdecor_libversion,
  include_directories: libdecor_includes,
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    wayland_client_dep,
    dl_dep,
    desktop_settings_dep,
  ],
  install: true
)

libdecor_dep = declare_dependency(
  link_with: libdecor,
  include_directories: [
    libdecor_includepath,
  ],
  dependencies: [
    wayland_client_dep,
  ],
)

meson.override_dependency(libdecor_full_name, libdecor_dep)

## decor plugins
subdir('plugins')

## pkg-config file
pkg.generate(
  name: libdecor_full_name,
  description: 'library for Wayland client-side window decors',
  libraries: libdecor,
  requires_private: [
    'wayland-client',
  ],
  subdirs: libdecor_full_name,
  version: meson.project_version()
)
