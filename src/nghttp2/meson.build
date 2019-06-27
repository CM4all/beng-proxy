libnghttp2 = dependency('libnghttp2')

nghttp2 = static_library('nghttp2',
  'Server.cxx',
  'IstreamDataSource.cxx',
  include_directories: inc,
)
nghttp2_dep = declare_dependency(
  link_with: nghttp2,
  dependencies: [
    libnghttp2,
    http_util_dep,
    http_common_dep,
    istream_dep,
    socket_dep,
  ],
)
