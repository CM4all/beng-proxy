control_server = static_library(
  'control_server',
  'Local.cxx',
  include_directories: inc,
  dependencies: control_server_dep,
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
  include_directories: inc,
  dependencies: [
    fmt_dep,
    control_client_dep,
    net_dep,
    io_dep,
    util_dep,
    libcxx,
  ],
  install: true,
)
