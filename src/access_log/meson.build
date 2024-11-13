access_log_client = static_library(
  'access_log',
  'Client.cxx',
  'Launch.cxx',
  'Glue.cxx',
  'Multi.cxx',
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

executable(
  'cm4all-beng-proxy-log-cat',
  'Server.cxx',
  'Cat.cxx',
  include_directories: inc,
  dependencies: [
    net_log_dep,
    http_dep,
  ],
  install: true,
)

executable(
  'cm4all-beng-proxy-log-json',
  'Server.cxx',
  'Json.cxx',
  include_directories: inc,
  dependencies: [
    net_log_dep,
    http_dep,
  ],
  install: true,
)

if lua_dep.found()
  executable(
    'cm4all-beng-proxy-log-lua',
    'Server.cxx',
    'Lua.cxx',
    'Launch.cxx',
    include_directories: inc,
    dependencies: [
      lua_dep,
      fmt_dep,
      spawn_options_dep,
      net_log_dep,
      http_dep,
      liblua,
    ],
    install: true,
  )
endif

executable(
  'cm4all-beng-proxy-log-traffic',
  'Server.cxx',
  'Traffic.cxx',
  include_directories: inc,
  dependencies: [
    net_log_dep,
    http_dep,
    fmt_dep,
  ],
  install: true,
)

executable(
  'cm4all-beng-proxy-log-split',
  'Server.cxx',
  'Split.cxx',
  include_directories: inc,
  dependencies: [
    system_dep,
    net_log_dep,
    http_dep,
  ],
  install: true,
)

executable(
  'cm4all-beng-proxy-log-forward',
  'Forward.cxx',
  include_directories: inc,
  dependencies: [
    net_log_dep,
    http_dep,
  ],
  install: true,
)

executable(
  'cm4all-beng-proxy-log-exec',
  'Exec.cxx',
  include_directories: inc,
  dependencies: [
    net_log_dep,
  ],
  install: true,
)

executable(
  'cm4all-beng-proxy-log-tee',
  'Launch.cxx',
  'Tee.cxx',
  include_directories: inc,
  dependencies: [
    spawn_options_dep,
    system_dep,
    net_log_dep,
  ],
  install: true,
)
