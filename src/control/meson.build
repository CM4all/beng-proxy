control_server = static_library(
  'control_server',
  'Server.cxx',
  'Local.cxx',
  include_directories: inc,
)

control_server_dep = declare_dependency(
  link_with: control_server,
  dependencies: [
    event_net_dep,
  ],
)

executable(
  'cm4all-beng-control',
  'Main.cxx',
  'Client.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
    io_dep,
    util_dep,
    libcxx,
  ],
  install: true,
)
