libwas = dependency('libcm4all-was-protocol', version: '>= 1.21', required: get_option('was'))
if not libwas.found()
  was_client_dep = dependency('', required: false)
  subdir_done()
endif

was_common = static_library(
  'was_common',
  'Map.cxx',
  'Output.cxx',
  'Input.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
    was_async_dep,
  ],
)

was_common_dep = declare_dependency(
  link_with: was_common,
  dependencies: [
    istream_dep,
    event_net_dep,
    event_dep,
    net_dep,
    putil_dep,
    http_dep,
  ],
)

was_client = static_library(
  'was_client',
  'Client.cxx',
  'IdleConnection.cxx',
  'Stock.cxx',
  'Launch.cxx',
  'Glue.cxx',
  'MStock.cxx',
  'MGlue.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
  ],
)
was_client_dep = declare_dependency(
  compile_args: '-DHAVE_LIBWAS',
  link_with: was_client,
  dependencies: [
    spawn_dep,
    raddress_dep,
    was_common_dep,
    stopwatch_dep,
  ],
)

was_server = static_library(
  'was_server',
  'Server.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
    was_common_dep,
  ],
)
was_server_dep = declare_dependency(
  link_with: was_server,
  dependencies: [
    libwas,
  ],
)
