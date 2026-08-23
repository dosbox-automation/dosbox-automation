cairo_dep = dependency('cairo')
math_dep = cc.find_library('m', required: true)

plugin_common = static_library('cairo-blur',
  sources: [
    'libdecor-cairo-blur.c',
  ],
  dependencies: [
    cairo_dep,
    math_dep,
  ],
  gnu_symbol_visibility: 'hidden',
  install: false,
)
