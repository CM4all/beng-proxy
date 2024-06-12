http_server = static_library('http_server',
  '../IncomingRequest.cxx',
  'SRequest.cxx',
  'Public.cxx',
  'Send.cxx',
  'Request.cxx',
  'Read.cxx',
  'Response.cxx',
  include_directories: inc,
  dependencies: [
    memory_dep,
    net_log_dep,
    http_extra_dep,
  ],
)

http_server_dep = declare_dependency(
  link_with: http_server,
  dependencies: [
    http_util_dep,
    http_common_dep,
    memory_istream_dep,
    putil_dep,
    socket_dep,
    stopwatch_dep,
  ],
)
