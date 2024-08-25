libwas = dependency('libcm4all-was-protocol', version: '>= 1.24', required: get_option('was'))
if not libwas.found()
  was_client_dep = dependency('', required: false)
  was_stock_dep = dependency('', required: false)
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
  'Launch.cxx',
  'IdleConnection.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
    fmt_dep,
    was_common_dep,
    spawn_dep,
    stopwatch_dep,
  ],
)
was_client_dep = declare_dependency(
  link_with: was_client,
)

was_stock = static_library(
  'was_stock',
  'SConnection.cxx',
  'Stock.cxx',
  'SLease.cxx',
  'SRequest.cxx',
  'Glue.cxx',
  'MStock.cxx',
  'MGlue.cxx',
  'RStock.cxx',
  include_directories: inc,
  dependencies: [
    io_dep,
    stock_dep,
    was_client_dep,
    libwas,
  ],
)
was_stock_dep = declare_dependency(
  compile_args: '-DHAVE_LIBWAS',
  link_with: was_stock,
  dependencies: [
    spawn_dep,
    raddress_dep,
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
    fmt_dep,
  ],
)
was_server_dep = declare_dependency(
  link_with: was_server,
  dependencies: [
    libwas,
  ],
)
