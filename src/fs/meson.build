socket = static_library(
  'socket',
  'FilteredSocket.cxx',
  'Ptr.cxx',
  'NopSocketFilter.cxx',
  'NopThreadSocketFilter.cxx',
  'ThreadSocketFilter.cxx',
  'Listener.cxx',
  'Connect.cxx',
  'Lease.cxx',
  'Stock.cxx',
  'Key.cxx',
  'Balancer.cxx',
  include_directories: inc,
)

socket_dep = declare_dependency(
  link_with: socket,
  dependencies: [
    event_net_dep,
  ],
)
