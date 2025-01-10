fcgi_client = static_library(
  'fcgi_client',
  'Client.cxx',
  'Serialize.cxx',
  'istream_fcgi.cxx',
  include_directories: inc,
  dependencies: [
    event_net_dep,
    istream_dep,
    memory_istream_dep,
    http_util_dep,
    stopwatch_dep,
    fmt_dep,
  ],
)

fcgi_client_dep = declare_dependency(
  link_with: fcgi_client,
  dependencies: [
  ],
)

fcgi_stock = static_library(
  'fcgi_stock',
  'Remote.cxx',
  'Request.cxx',
  'Stock.cxx',
  'Connection.cxx',
  include_directories: inc,
  dependencies: [
    fcgi_client_dep,
    stopwatch_dep,
    event_dep,
    fmt_dep,
  ],
)

fcgi_stock_dep = declare_dependency(
  link_with: fcgi_stock,
  dependencies: [
  ],
)
