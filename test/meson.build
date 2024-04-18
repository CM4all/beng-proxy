gtest = dependency('gtest', main: true,
                   include_type: 'system',
                   disabler: true,
                   required: get_option('test'),
                   fallback: ['gtest', 'gtest_main_dep'])

test_instance = static_library(
  'TestInstance',
  'TestInstance.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    event_dep,
    pool_dep,
  ],
)

test_instance_dep = declare_dependency(
  link_with: test_instance,
  dependencies: [
    event_dep,
    pool_dep,
  ],
)

executable('dump_control',
  'dump_control.cxx',
  include_directories: inc,
  dependencies: [
    control_server_dep,
    system_dep,
  ])

executable('run_parser_cdata',
  'run_parser_cdata.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    processor_dep,
  ])

executable('run_css_parser',
  'run_css_parser.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    processor_dep,
  ])

executable('run_client',
  'run_client.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    istream_pipe_dep,
    ssl_dep,
    was_client_dep,
    http_client_dep,
    nghttp2_client_dep,
    event_net_dep,
    stock_dep,
    socket_dep,
    thread_pool_dep,
  ])

executable(
  'RunAnyHttpClient',
  'RunAnyHttpClient.cxx',
  '../src/http/AnyClient.cxx',
  '../src/http/GlueClient.cxx',
  '../src/ssl/SslSocketFilterFactory.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    istream_pipe_dep,
    ssl_dep,
    was_client_dep,
    http_client_dep,
    nghttp2_client_dep,
    event_net_dep,
    stock_dep,
    socket_dep,
    cluster_dep,
    net_dep,
    thread_pool_dep,
    raddress_dep,
  ])

if nghttp2_client_dep.found()
  executable(
    'http2_rapid_reset',
    'http2_rapid_reset.cxx',
    '../src/ssl/SslSocketFilterFactory.cxx',
    include_directories: inc,
    dependencies: [
      test_instance_dep,
      istream_pipe_dep,
      ssl_dep,
      was_client_dep,
      http_client_dep,
      nghttp2_client_dep,
      event_net_dep,
      stock_dep,
      socket_dep,
      cluster_dep,
      net_dep,
      thread_pool_dep,
      raddress_dep,
    ],
  )
endif

executable('run_delegate',
  'run_delegate.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    delegate_client_dep,
    spawn_dep,
  ])

executable('run_http_cache',
  'run_http_cache.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    http_cache_dep,
  ])

executable('run_http_server',
  'run_http_server.cxx',
  'DemoHttpServerConnection.cxx',
  '../src/address_string.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    http_server_dep,
    system_dep,
  ])

if libnghttp2.found()
  executable(
    'RunNgHttp2Server',
    'RunNgHttp2Server.cxx',
    '../src/address_string.cxx',
    '../src/http/IncomingRequest.cxx',
    include_directories: inc,
    dependencies: [
      fmt_dep,
      nghttp2_server_dep,
      putil_dep,
    ],
  )
endif

executable('run_ping',
  'run_ping.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    pool_dep,
    event_net_dep,
  ])

if libwas.found()
  executable(
    'run_was',
    'run_was.cxx',
    include_directories: inc,
    dependencies: [
      test_instance_dep,
      fmt_dep,
      expand_dep,
      net_dep,
      was_client_dep,
      stopwatch_dep,
    ],
  )

  executable(
    'was_mirror',
    'was_mirror.cxx',
    include_directories: inc,
    dependencies: [
      test_instance_dep,
      was_server_dep,
    ],
  )
endif

executable(
  'LaunchLocalHTTP',
  'LaunchLocalHTTP.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    expand_dep,
    event_dep,
    pool_dep,
    net_dep,
    was_client_dep,
    stopwatch_dep,
  ],
)

executable(
  'request_translation',
  'request_translation.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    translation_dep,
    widget_dep,
    stock_dep,
    http_dep,
    cookie_dep,
    fmt_dep,
  ],
)

executable('run_subst',
  'run_subst.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    istream_dep,
  ])

executable('run_html_unescape',
  'run_html_unescape.cxx',
  '../src/escape/Static.cxx',
  include_directories: inc,
  dependencies: [
    util_dep,
  ])

executable('run_html_escape',
  'run_html_escape.cxx',
  '../src/escape/Static.cxx',
  include_directories: inc,
  dependencies: [
    util_dep,
  ])

executable('run_processor',
  'run_processor.cxx',
  'FailingResourceLoader.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/escape/Istream.cxx',
  '../src/istream_html_escape.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    processor_dep,
    widget_dep,
    session_dep,
  ])

executable('run_client_balancer',
  'run_client_balancer.cxx',
  '../src/cluster/ConnectBalancer.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    event_net_dep,
    stopwatch_dep,
    eutil_dep,
    raddress_dep,
    cluster_dep,
  ])

if avahi_dep.found()
  executable(
    'DumpZeroconfHashRing',
    'DumpZeroconfHashRing.cxx',
    '../src/lb/MemberHash.cxx',
    include_directories: inc,
    dependencies: [
      test_instance_dep,
      avahi_dep,
      net_dep,
      sodium_dep,
      fmt_dep,
    ],
  )
endif

test(
  't_cache',
  executable(
    't_cache',
    't_cache.cxx',
    '../src/PInstance.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      eutil_dep,
      pool_dep,
    ],
  ),
)

test(
  't_tcache',
  executable(
    't_tcache',
    't_tcache.cxx',
    'RecordingTranslateHandler.cxx',
    '../src/PInstance.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      libcommon_translation_dep,
      eutil_dep,
      raddress_dep,
      stopwatch_dep,
      widget_dep,
    ],
  ),
)

test('t_regex', executable('t_regex',
  't_regex.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    expand_dep,
    pcre_dep,
  ]))

test('t_header_forward', executable('t_header_forward',
  't_header_forward.cxx',
  '../src/bp/ForwardHeaders.cxx',
  '../src/bp/session/Id.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    pcre_dep,
    putil_dep,
    http_dep,
    cookie_dep,
    system_dep,
  ]))

test('t_resource_address', executable('t_resource_address',
  't_resource_address.cxx',
  't_http_address.cxx',
  't_lhttp_address.cxx',
  't_cgi_address.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    raddress_dep,
  ]))

test('t_balancer', executable('t_balancer',
  't_balancer.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    eutil_dep,
    pool_dep,
    net_dep,
    cluster_dep,
    raddress_dep,
  ]))

test(
  'TestFilteredSocket',
  executable(
    'TestFilteredSocket',
    'TestFilteredSocket.cxx',
    'EchoSocket.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      socket_dep,
    ],
  ),
)

test('t_cgi', executable('t_cgi',
  't_cgi.cxx',
  '../src/http/ResponseHandler.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    cgi_dep,
    system_dep,
    raddress_dep,
  ]),
  env: ['srcdir=' + meson.project_source_root()],
)

t_client_sources = []
t_client_dependencies = []

if libwas.found()
  t_client_sources += 't_was_client.cxx'
  t_client_dependencies += [was_client_dep, was_server_dep]
endif

test(
  't_client',
  executable(
    't_client',
    't_client.cxx',
    't_http_client.cxx',
    't_fcgi_client.cxx',
    'fcgi_server.cxx',
    'DemoHttpServerConnection.cxx',
    '../src/address_string.cxx',
    t_client_sources,
    include_directories: inc,
    dependencies: [
      gtest,
      test_instance_dep,
      istream_pipe_dep,
      http_client_dep,
      http_server_dep,
      stock_dep,
      system_dep,
      thread_pool_dep,
      fcgi_client_dep,
      t_client_dependencies,
    ],
  ),
)

test('t_http_server', executable('t_http_server',
  't_http_server.cxx',
  '../src/address_string.cxx',
  include_directories: inc,
  dependencies: [
    test_instance_dep,
    fmt_dep,
    http_server_dep,
    http_client_dep,
    system_dep,
  ]))

test('t_http_cache', executable('t_http_cache',
  't_http_cache.cxx',
  'RecordingHttpResponseHandler.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    test_instance_dep,
    putil_dep,
    http_cache_dep,
  ]))

test('t_fcache', executable('t_fcache',
  't_fcache.cxx',
  'BlockingResourceLoader.cxx',
  'MirrorResourceLoader.cxx',
  '../src/http/cache/FilterCache.cxx',
  '../src/cache.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
    memory_istream_dep,
    istream_dep,
    raddress_dep,
    http_dep,
    stopwatch_dep,
  ]))

test('t_session', executable('t_session',
  't_session.cxx',
  'TestSessionId.cxx',
  'TestCsrfProtection.cxx',
  'TestSessionModulo.cxx',
  '../src/bp/CsrfToken.cxx',
  '../src/lb/Session.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    session_dep,
  ]))

test('t_escape', executable('t_escape',
  't_html_escape.cxx',
  't_escape_css.cxx',
  '../src/escape/Static.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    util_dep,
  ]))

test('t_processor', executable('t_processor',
  'FailingResourceLoader.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/escape/Istream.cxx',
  '../src/istream_html_escape.cxx',
  't_processor.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    test_instance_dep,
    processor_dep,
    widget_dep,
    session_dep,
  ]))

test(
  'TestBufferedResourceLoader',
  executable(
    'TestBufferedResourceLoader',
    'TestBufferedResourceLoader.cxx',
    'FailingResourceLoader.cxx',
    'BlockingResourceLoader.cxx',
    'MirrorResourceLoader.cxx',
    'RecordingHttpResponseHandler.cxx',
    '../src/BufferedResourceLoader.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      test_instance_dep,
      istream_dep,
      istream_pipe_dep,
      putil_dep,
      stock_dep,
      stopwatch_dep,
      http_dep,
    ],
  ),
)  

test(
  'TestAprMd5',
  executable(
    'TestAprMd5',
    'TestAprMd5.cxx',
    '../src/bp/AprMd5.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      libcrypto,
    ],
  ),
)

if get_option('certdb')
  executable(
    'RunNameCache',
    'RunNameCache.cxx',
    include_directories: inc,
    dependencies: [
      ssl_dep,
      pg_dep,
    ],
  )
endif

if uring_dep.found()
  executable(
    'RunUringIstream',
    'RunUringIstream.cxx',
    include_directories: inc,
    dependencies: [
      test_instance_dep,
      istream_dep,
      event_uring_dep,
    ],
  )
endif

if get_option('certdb')
  test(
    'TestWrapKey',
    executable(
      'TestWrapKey',
      'TestWrapKey.cxx',
      '../src/certdb/WrapKey.cxx',
      include_directories: inc,
      dependencies: [
        crypto_dep,
        system_dep,
        sodium_dep,
        gtest,
      ],
    ),
  )
endif

subdir('acme')
subdir('http')
subdir('istream')
subdir('memory')
subdir('uri')
subdir('widget')
