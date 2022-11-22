access_log_client = static_library(
  'access_log',
  'Client.cxx',
  'Launch.cxx',
  'Glue.cxx',
  'ConfigParser.cxx',
  include_directories: inc,
  dependencies: [
    io_config_dep,
    net_dep,
  ],
)

access_log_client_dep = declare_dependency(
  link_with: access_log_client,
)
