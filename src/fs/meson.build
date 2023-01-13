socket = static_library(
  'socket',
  'FilteredSocket.cxx',
  'Ptr.cxx',
  'NopSocketFilter.cxx',
  'NopThreadSocketFilter.cxx',
  'ApproveThreadSocketFilter.cxx',
  'ThreadSocketFilter.cxx',
  'Listener.cxx',
  'Connect.cxx',
  'Lease.cxx',
  'Stock.cxx',
  'Key.cxx',
  'Balancer.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)

socket_dep = declare_dependency(
  link_with: socket,
  dependencies: [
    event_net_dep,
    thread_pool_dep,
  ],
)
