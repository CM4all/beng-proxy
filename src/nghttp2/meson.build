libnghttp2 = dependency('libnghttp2', required: get_option('http2'))
if not libnghttp2.found()
  nghttp2_dep = libnghttp2
  subdir_done()
endif

nghttp2 = static_library('nghttp2',
  'Server.cxx',
  'Client.cxx',
  'Stock.cxx',
  'Glue.cxx',
  'IstreamDataSource.cxx',
  include_directories: inc,
)
nghttp2_dep = declare_dependency(
  compile_args: '-DHAVE_NGHTTP2',
  link_with: nghttp2,
  dependencies: [
    libnghttp2,
    http_util_dep,
    http_common_dep,
    istream_dep,
    socket_dep,
    stopwatch_dep,
  ],
)
