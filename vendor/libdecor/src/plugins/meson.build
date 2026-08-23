gtk_dep = dependency('gtk+-3.0', required: get_option('gtk'))

plugin_include_path = include_directories('.')

subdir('common')
subdir('cairo')
subdir('dummy')

if gtk_dep.found()
  subdir('gtk')
endif
