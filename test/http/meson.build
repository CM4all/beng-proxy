executable(
  'format_http_date',
  'format_http_date.cxx',
  include_directories: inc,
  dependencies: [
    time_dep,
    http_dep,
  ],
)

executable(
  'run_cookie_client',
  'run_cookie_client.cxx',
  include_directories: inc,
  dependencies: [
    cookie_dep,
  ],
)

executable(
  'run_cookie_server',
  'run_cookie_server.cxx',
  include_directories: inc,
  dependencies: [
    cookie_dep,
  ],
)

executable(
  'run_header_parser',
  'run_header_parser.cxx',
  include_directories: inc,
  dependencies: [
    http_util_dep,
  ],
)

test(
  'TestHttpUtil',
  executable(
    'TestHttpUtil',
    'TestXFF.cxx',
    include_directories: inc,
    dependencies: [
      http_util_dep,
      gtest,
    ],
  ),
)

test(
  't_cookie',
  executable(
    't_cookie',
    't_cookie_client.cxx',
    't_cookie_server.cxx',
    'TestCookieExtract.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      cookie_dep,
    ],
  ),
)
