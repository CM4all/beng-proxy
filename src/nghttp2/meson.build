libnghttp2 = dependency('libnghttp2', required: get_option('http2'))
if not libnghttp2.found()
  nghttp2_features_dep = libnghttp2
  nghttp2_server_dep = libnghttp2
  nghttp2_client_dep = libnghttp2
  subdir_done()
endif

nghttp2_features_dep = declare_dependency(
  compile_args: '-DHAVE_NGHTTP2',
)

nghttp2_common = static_library(
  'nghttp2_common',
  'Error.cxx',
  'SocketUtil.cxx',
  'IstreamDataSource.cxx',
  include_directories: inc,
  dependencies: [
    libnghttp2,
    istream_dep,
    socket_dep,
  ],
)

nghttp2_common_dep = declare_dependency(
  link_with: nghttp2_common,
  dependencies: [
    libnghttp2,
    istream_dep,
    socket_dep,
  ],
)

nghttp2_server = static_library(
  'nghttp2_server',
  'Server.cxx',
  include_directories: inc,
  dependencies: [
    nghttp2_common_dep,
    fmt_dep,
    http_util_dep,
    socket_dep,
    stopwatch_dep,
  ],
)

nghttp2_server_dep = declare_dependency(
  link_with: nghttp2_server,
  dependencies: [
    libnghttp2,
    nghttp2_features_dep,
  ],
)

nghttp2_client = static_library(
  'nghttp2_client',
  'Client.cxx',
  'Stock.cxx',
  'Glue.cxx',
  include_directories: inc,
  dependencies: [
    nghttp2_common_dep,
    fmt_dep,
    http_util_dep,
    socket_dep,
    stopwatch_dep,
  ],
)

nghttp2_client_dep = declare_dependency(
  link_with: nghttp2_client,
  dependencies: [
    libnghttp2,
    nghttp2_features_dep,
  ],
)
