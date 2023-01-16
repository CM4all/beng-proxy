fcgi_client = static_library(
  'fcgi_client',
  'Client.cxx',
  'Remote.cxx',
  'Request.cxx',
  'Serialize.cxx',
  'Stock.cxx',
  'istream_fcgi.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)

fcgi_client_dep = declare_dependency(
  link_with: fcgi_client,
  dependencies: [
    socket_dep,
    putil_dep,
    istream_dep,
    memory_istream_dep,
    http_util_dep,
    stopwatch_dep,
  ],
)
