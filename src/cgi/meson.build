cgi = static_library(
  'cgi',
  'Glue.cxx',
  'Parser.cxx',
  'Client.cxx',
  'Launch.cxx',
  include_directories: inc,
)

cgi_dep = declare_dependency(
  link_with: cgi,
  dependencies: [
    stopwatch_dep,
    istream_spawn_dep,
    http_util_dep,
  ],
)
