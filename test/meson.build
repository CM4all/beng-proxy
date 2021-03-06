gtest_compile_args = [
  '-Wno-undef',
]

if compiler.get_id() == 'gcc'
  gtest_compile_args += [
    '-Wno-suggest-attribute=format',
    '-Wno-suggest-attribute=noreturn',
    '-Wno-missing-declarations',
  ]
endif

if compiler.get_id() == 'clang'
  gtest_compile_args += [
    # TYPED_TEST_CASE_P got deprecated in GTest 1.9.0
    '-Wno-deprecated-declarations',

    # clang throws this when using INSTANTIATE_TYPED_TEST_SUITE_P()
    '-Wno-gnu-zero-variadic-macro-arguments',
  ]
endif

if compiler.get_id() == 'clang' and compiler.version().version_compare('>=10')
  gtest_compile_args += [
    # work around clang warning caused by GTest's wrong "-lpthread"
    # compiler flag
    '-Wno-unused-command-line-argument',
  ]
endif

gtest_main_dep = dependency('gtest', main: true, fallback: ['gtest', 'gtest_main_dep'])
gtest = declare_dependency(
  dependencies: [gtest_main_dep],
  compile_args: gtest_compile_args,
)

executable('dump_udp',
  'dump_udp.cxx',
  include_directories: inc,
  dependencies: [
    event_net_dep,
  ])

executable('dump_control',
  'dump_control.cxx',
  include_directories: inc,
  dependencies: [
    control_server_dep,
    system_dep,
  ])

executable('run_parser_cdata',
  'run_parser_cdata.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    processor_dep,
  ])

executable('run_css_parser',
  'run_css_parser.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    processor_dep,
  ])

executable('run_client',
  'run_client.cxx',
  '../src/PInstance.cxx',
  '../src/istream_gb.cxx',
  '../src/pipe_stock.cxx',
  '../src/PipeLease.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    ssl_dep,
    was_client_dep,
    http_client_dep,
    nghttp2_dep,
    event_net_dep,
    stock_dep,
    socket_dep,
    thread_pool_dep,
  ])

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
  '../src/istream_rubber.cxx',
  '../src/istream_unlock.cxx',
  include_directories: inc,
  dependencies: [
    http_cache_dep,
  ])

executable('run_http_server',
  'run_http_server.cxx',
  'DemoHttpServerConnection.cxx',
  '../src/PInstance.cxx',
  '../src/address_string.cxx',
  '../src/istream_gb.cxx',
  '../src/duplex.cxx',
  include_directories: inc,
  dependencies: [
    http_server_dep,
    system_dep,
  ])

if libnghttp2.found()
  executable(
    'RunNgHttp2Server',
    'RunNgHttp2Server.cxx',
    '../src/address_string.cxx',
    '../src/http/IncomingRequest.cxx',
    '../src/nghttp2/Server.cxx',
    include_directories: inc,
    dependencies: [
      nghttp2_dep,
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
    '../src/PInstance.cxx',
    '../src/pexpand.cxx',
    include_directories: inc,
    dependencies: [
      net_dep,
      was_client_dep,
      stopwatch_dep,
    ],
  )

  executable(
    'was_mirror',
    'was_mirror.cxx',
    '../src/PInstance.cxx',
    include_directories: inc,
    dependencies: [
      was_server_dep,
    ],
  )
endif

executable('format_http_date',
  'format_http_date.cxx',
  include_directories: inc,
  dependencies: [
    time_dep,
    http_dep,
  ])

executable('request_translation',
  'request_translation.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    translation_dep,
    widget_dep,
    stock_dep,
    http_dep,
  ])

executable('run_subst',
  'run_subst.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    istream_dep,
  ])

if libyamlcpp.found()
  executable(
    'RunYamlSubst',
    'RunYamlSubst.cxx',
    '../src/PInstance.cxx',
    include_directories: inc,
    dependencies: [
      istream_dep,
    ],
  )
endif

executable('run_cookie_client',
  'run_cookie_client.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    cookie_dep,
  ])

executable('run_cookie_server',
  'run_cookie_server.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    cookie_dep,
  ])

executable('run_header_parser',
  'run_header_parser.cxx',
  include_directories: inc,
  dependencies: [
    http_util_dep,
  ])

executable('run_html_unescape',
  'run_html_unescape.cxx',
  '../src/escape_static.cxx',
  include_directories: inc,
  dependencies: [
    util_dep,
  ])

executable('run_html_escape',
  'run_html_escape.cxx',
  '../src/escape_static.cxx',
  include_directories: inc,
  dependencies: [
    util_dep,
  ])

executable('run_processor',
  'run_processor.cxx',
  'FailingResourceLoader.cxx',
  '../src/PInstance.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/random.cxx',
  '../src/istream_escape.cxx',
  '../src/istream_html_escape.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    processor_dep,
    widget_dep,
    session_dep,
  ])

executable('run_client_balancer',
  'run_client_balancer.cxx',
  '../src/PInstance.cxx',
  '../src/cluster/ConnectBalancer.cxx',
  include_directories: inc,
  dependencies: [
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
    '../src/PInstance.cxx',
    '../src/lb/MemberHash.cxx',
    include_directories: inc,
    dependencies: [
      pool_dep,
      avahi_dep,
      net_dep,
      sodium_dep,
    ],
  )
endif

if nfs_client_dep.found()
  executable(
    'run_nfs_client',
    'run_nfs_client.cxx',
    '../src/PInstance.cxx',
    '../src/pipe_stock.cxx',
    '../src/PipeLease.cxx',
    '../src/nfs/Istream.cxx',
    include_directories: inc,
    dependencies: [
      nfs_client_dep,
      istream_dep,
      stock_dep,
      io_dep,
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
  ]))

test('t_header_forward', executable('t_header_forward',
  't_header_forward.cxx',
  '../src/bp/ForwardHeaders.cxx',
  '../src/random.cxx',
  '../src/bp/session/Id.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    pcre_dep,
    putil_dep,
    http_dep,
    cookie_dep,
    system_dep,
  ]))

test('TestRedirectHttps', executable('TestRedirectHttps',
  'TestRedirectHttps.cxx',
  '../src/RedirectHttps.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    putil_dep,
    net_dep,
  ]))

test(
  't_stock',
  executable(
    't_stock',
    't_stock.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      stock_dep,
    ],
  ),
)

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

test('t_expansible_buffer', executable('t_expansible_buffer',
  't_expansible_buffer.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    putil_dep,
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

test('t_cgi', executable('t_cgi',
  't_cgi.cxx',
  '../src/PInstance.cxx',
  '../src/http/ResponseHandler.cxx',
  include_directories: inc,
  dependencies: [
    cgi_dep,
    system_dep,
    raddress_dep,
  ]),
  env: ['srcdir=' + meson.source_root()],
)

test('t_http_client', executable('t_http_client',
  't_http_client.cxx',
  'DemoHttpServerConnection.cxx',
  '../src/PInstance.cxx',
  '../src/istream_gb.cxx',
  '../src/address_string.cxx',
  include_directories: inc,
  dependencies: [
    http_client_dep,
    http_server_dep,
    system_dep,
  ]),
  env: ['srcdir=' + meson.source_root()],
)

test('t_http_server', executable('t_http_server',
  't_http_server.cxx',
  '../src/PInstance.cxx',
  '../src/address_string.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    http_server_dep,
    http_client_dep,
    system_dep,
  ]))

test('t_fcgi_client', executable('t_fcgi_client',
  't_fcgi_client.cxx',
  'fcgi_server.cxx',
  '../src/PInstance.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    fcgi_client_dep,
  ]),
)

if libwas.found()
  test(
    't_was_client',
    executable(
      't_was_client',
      't_was_client.cxx',
      '../src/PInstance.cxx',
      include_directories: inc,
      dependencies: [
        was_client_dep,
        was_server_dep,
      ],
    ),
  )
endif

test('t_http_cache', executable('t_http_cache',
  't_http_cache.cxx',
  'RecordingHttpResponseHandler.cxx',
  '../src/PInstance.cxx',
  '../src/istream_unlock.cxx',
  '../src/istream_rubber.cxx',
  '../src/sink_rubber.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    putil_dep,
    http_cache_dep,
  ]))

test('t_fcache', executable('t_fcache',
  't_fcache.cxx',
  'BlockingResourceLoader.cxx',
  'MirrorResourceLoader.cxx',
  '../src/fcache.cxx',
  '../src/cache.cxx',
  '../src/sink_rubber.cxx',
  '../src/istream_rubber.cxx',
  '../src/istream_unlock.cxx',
  include_directories: inc,
  dependencies: [
    istream_dep,
    raddress_dep,
    http_dep,
    stopwatch_dep,
  ]))

test(
  't_cookie',
  executable(
    't_cookie',
    't_cookie_client.cxx',
    't_cookie_server.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      cookie_dep,
    ],
  ),
)

test('t_session', executable('t_session',
  't_session.cxx',
  'TestSessionId.cxx',
  '../src/random.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    session_dep,
  ]))

test('t_pool', executable('t_pool',
  't_pool.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    pool_dep,
  ]))

test('t_rubber', executable('t_rubber',
  't_rubber.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    memory_dep,
  ]))

test('t_sink_rubber', executable('t_sink_rubber',
  't_sink_rubber.cxx',
  '../src/sink_rubber.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    istream_dep,
  ]))

test('t_slice', executable('t_slice',
  't_slice.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    memory_dep,
  ]))

test('t_relocate_uri', executable('t_relocate_uri',
  't_relocate_uri.cxx',
  '../src/relocate_uri.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    putil_dep,
  ]))

test('t_rewrite_uri', executable('t_rewrite_uri',
  't_rewrite_uri.cxx',
  'FailingResourceLoader.cxx',
  '../src/PInstance.cxx',
  '../src/widget/RewriteUri.cxx',
  '../src/bp/session/Lease.cxx',
  '../src/istream_escape.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    putil_dep,
    widget_dep,
    istream_dep,
  ]))

test(
  'TestWidget',
  executable(
    'TestWidget',
    'TestArgs.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      widget_dep,
    ],
  ),
)

test('t_widget_registry', executable('t_widget_registry',
  't_widget_registry.cxx',
  '../src/PInstance.cxx',
  '../src/widget/Registry.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    widget_dep,
  ]))

test('t_widget_resolver', executable('t_widget_resolver',
  't_widget_resolver.cxx',
  '../src/widget/Resolver.cxx',
  '../src/widget/Init.cxx',
  '../src/widget/Root.cxx',
  '../src/widget/Ptr.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    putil_dep,
    istream_dep,
  ]))

test('t_wembed', executable('t_wembed',
  't_wembed.cxx',
  'FailingResourceLoader.cxx',
  '../src/PInstance.cxx',
  '../src/widget/Inline.cxx',
  '../src/istream_html_escape.cxx',
  '../src/istream_escape.cxx',
  '../src/bp/Global.cxx',
  include_directories: inc,
  dependencies: [
    widget_dep,
    istream_dep,
    http_util_dep,
  ]))

test('t_widget_http', executable('t_widget_http',
  't_widget_http.cxx',
  '../src/PInstance.cxx',
  '../src/widget/Request.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/bp/ForwardHeaders.cxx',
  '../src/resource_tag.cxx',
  '../src/random.cxx',
  '../src/http/ResponseHandler.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    widget_dep,
    istream_dep,
    http_util_dep,
    session_dep,
    processor_dep,
    time_dep,
    http_dep,
  ]))

test('t_uri', executable('t_uri',
  't_uri_relative.cxx',
  't_uri_compare.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    util_dep,
    putil_dep,
  ]))

test('t_escape', executable('t_escape',
  't_html_escape.cxx',
  't_escape_css.cxx',
  '../src/escape_static.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    util_dep,
  ]))

test('t_processor', executable('t_processor',
  'FailingResourceLoader.cxx',
  '../src/PInstance.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/random.cxx',
  '../src/istream_escape.cxx',
  '../src/istream_html_escape.cxx',
  't_processor.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    processor_dep,
    widget_dep,
    session_dep,
  ]))

t_istream_filter = static_library('t_istream_filter',
  '../src/PInstance.cxx',
  '../src/pipe_stock.cxx',
  '../src/PipeLease.cxx',
  include_directories: inc,
  dependencies: [
    eutil_dep,
    istream_dep,
  ])

t_istream_filter_deps = [
  gtest,
  istream_dep,
  declare_dependency(link_with: t_istream_filter),
]

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
    '../src/pipe_stock.cxx',
    '../src/PipeLease.cxx',
    '../src/PInstance.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      istream_dep,
      putil_dep,
      stock_dep,
      stopwatch_dep,
    ],
  ),
)  

istream_test_sources = []
if libyamlcpp.found()
  istream_test_sources += 'TestYamlSubstIstream.cxx'
  t_istream_filter_deps += libyamlcpp
endif

test(
  'IstreamFilterTest',
  executable(
    'IstreamFilterTest',
    'IstreamFilterTest.cxx',
    't_istream_byte.cxx',
    't_istream_cat.cxx',
    't_istream_catch.cxx',
    't_istream_chunked.cxx',
    't_istream_dechunk.cxx',
    't_istream_deflate.cxx',
    't_istream_delayed.cxx',
    't_istream_fcgi.cxx',
    '../src/fcgi/istream_fcgi.cxx',
    't_istream_hold.cxx',
    't_istream_html_escape.cxx',
    '../src/istream_escape.cxx',
    '../src/istream_html_escape.cxx',
    't_istream_iconv.cxx',
    't_istream_replace.cxx',
    't_istream_subst.cxx',
    'TestAutoPipeIstream.cxx',
    'TestBufferedIstream.cxx',
    '../src/pipe_stock.cxx',
    '../src/PipeLease.cxx',
    istream_test_sources,
    include_directories: inc,
    dependencies: [
      t_istream_filter_deps,
      stock_dep,
    ],
  ),
)

test('t_istream_processor', executable('t_istream_processor',
  't_istream_processor.cxx',
  'IstreamFilterTest.cxx',
  'FailingResourceLoader.cxx',
  '../src/random.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/widget/Resolver.cxx',
  '../src/widget/RewriteUri.cxx',
  '../src/istream_escape.cxx',
  '../src/istream_html_escape.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    processor_dep,
    widget_dep,
    session_dep,
  ] + t_istream_filter_deps))

test(
  'TestCssProcessor',
  executable(
    'TestCssProcessor',
    'TestCssProcessor.cxx',
    'IstreamFilterTest.cxx',
    'FailingResourceLoader.cxx',
    '../src/random.cxx',
    '../src/widget/FromSession.cxx',
    '../src/widget/FromRequest.cxx',
    '../src/widget/Resolver.cxx',
    '../src/widget/RewriteUri.cxx',
    '../src/istream_escape.cxx',
    '../src/istream_html_escape.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      threads,
      processor_dep,
      widget_dep,
      session_dep,
    ] + t_istream_filter_deps,
  ),
)

test('t_istream_tee', executable('t_istream_tee',
  't_istream_tee.cxx',
  '../src/istream/sink_close.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: istream_dep))

test('t_growing_buffer', executable('t_growing_buffer',
  't_growing_buffer.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    istream_api_dep,
  ]))

if uring_dep.found()
  test(
    'TestUringIstream',
    executable(
      'TestUringIstream',
      'TestUringIstream.cxx',
      include_directories: inc,
      dependencies: [
        gtest,
        uring_dep,
        istream_dep,
      ],
    ),
  )
endif

test(
  'TestAprMd5',
  executable(
    'TestAprMd5',
    'TestAprMd5.cxx',
    '../src/bp/AprMd5.cxx',
    include_directories: inc,
    dependencies: [
      gtest,
      libssl,
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
    '../src/PInstance.cxx',
    include_directories: inc,
    dependencies: [
      istream_dep,
      event_uring_dep,
    ],
  )
endif
