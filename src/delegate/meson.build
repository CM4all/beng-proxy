delegate_client = static_library(
  'delegate_client',
  'Client.cxx',
  'Glue.cxx',
  'HttpRequest.cxx',
  'Stock.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)

delegate_client_dep = declare_dependency(
  link_with: delegate_client,
  dependencies: [
    socket_dep,
    putil_dep,
    stock_dep,
  ],
)
