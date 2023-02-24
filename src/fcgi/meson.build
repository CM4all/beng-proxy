fcgi_client = static_library(
  'fcgi_client',
  'Client.cxx',
  'Serialize.cxx',
  'istream_fcgi.cxx',
  include_directories: inc,
  dependencies: [
  ],
)

fcgi_client_dep = declare_dependency(
  link_with: fcgi_client,
  dependencies: [
    socket_dep,
    istream_dep,
    memory_istream_dep,
    http_util_dep,
    stopwatch_dep,
  ],
)

fcgi_stock = static_library(
  'fcgi_stock',
  'Remote.cxx',
  'Request.cxx',
  'Stock.cxx',
  include_directories: inc,
  dependencies: [
    fcgi_client_dep,
    fmt_dep,
  ],
)

fcgi_stock_dep = declare_dependency(
  link_with: fcgi_stock,
  dependencies: [
    socket_dep,
    istream_dep,
    stopwatch_dep,
  ],
)
