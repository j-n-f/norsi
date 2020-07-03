wayland_scanner_dep = dependency(
  'wayland-scanner', required: true, native: true
)
if wayland_scanner_dep.found()
  wayland_scanner = find_program('wayland-scanner', native: true)
endif

protocols = {
  'kde-idle': 'idle.xml'
}

foreach name, path: protocols
  client_code = custom_target(
    name.underscorify() + '_c',
    input: path,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']
  )
  norsi_files += client_code

  client_header = custom_target(
    name.underscorify() + 'client_h',
    input: path,
    output: '@BASENAME@-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']
  )
  norsi_files += client_header
endforeach
