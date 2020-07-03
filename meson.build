project(
  'norsi',
  'c',
  version: '0.1.0',
  license: 'GPL3',
  meson_version: '>=0.54.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=true',
  ]
)

cc = meson.get_compiler('c')

# Be strict
add_project_arguments(cc.get_supported_arguments([
	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wstrict-prototypes',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',
	'-Wmissing-prototypes',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

# Needed for `clock_gettime` (see man page)
add_project_arguments([
  '-D_POSIX_C_SOURCE=199309L'
], language: 'c')

waylandclient_dep = dependency('wayland-client')
rt_dep = cc.find_library('rt', required: true)

# All targets that have to be generated to build noRSI
norsi_files = []

# Wayland protocol definitions are needed to generate some code/headers
subdir('protocol')

norsi_deps = declare_dependency(sources: norsi_files)

proto_inc = include_directories('protocol')
other_inc = include_directories('include')

executable('norsi', 'main.c', 'safety-tracker.c', 'query-handler.c',
  dependencies : [waylandclient_dep, rt_dep, norsi_deps],
  include_directories: [proto_inc, other_inc],
)
