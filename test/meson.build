executable('TrafoExample',
  '../src/trafo/Server.cxx',
  '../src/trafo/Listener.cxx',
  '../src/trafo/Connection.cxx',
  '../src/trafo/AllocatedRequest.cxx',
  '../src/trafo/Response.cxx',
  'TrafoExample.cxx',
  include_directories: inc,
  dependencies: [
    event_net_dep,
    system_dep,
  ])

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

executable('send_control',
  'send_control.cxx',
  include_directories: inc,
  dependencies: [
    control_server_dep,
  ])

executable('run_ua_parser',
  'run_ua_parser.cxx',
  '../src/ua_classification.cxx',
  include_directories: inc,
  dependencies: [
    expand_dep,
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
  '../src/direct.cxx',
  '../src/serialize.cxx',
  '../src/istream_gb.cxx',
  '../src/pipe_stock.cxx',
  '../src/PipeLease.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    ssl_dep,
    was_client_dep,
    http_client_dep,
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
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/address_string.cxx',
  '../src/istream_gb.cxx',
  '../src/duplex.cxx',
  include_directories: inc,
  dependencies: [
    http_server_dep,
    system_dep,
  ])

executable('run_memcached_client',
  'run_memcached_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/pipe_stock.cxx',
  '../src/PipeLease.cxx',
  '../src/PipeLease.cxx',
  include_directories: inc,
  dependencies: [
    memcached_client_dep,
    stock_dep,
    net_dep,
    io_dep,
  ])

executable('fake_memcached_server',
  'fake_memcached_server.cxx',
  include_directories: inc,
  dependencies: [
  ])

executable('run_ping',
  'run_ping.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    pool_dep,
    event_net_dep,
  ])

executable('run_was',
  'run_was.cxx',
  '../src/PInstance.cxx',
  '../src/pexpand.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
    was_client_dep,
    stopwatch_dep,
  ])

executable('was_mirror',
  'was_mirror.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    declare_dependency(link_with: was_server),
  ])

executable('benchmark-gmtime',
  'benchmark-gmtime.c',
  'libcore-gmtime.c',
  include_directories: inc,
  dependencies: [
    util_dep,
    time_dep,
  ])

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
  '../src/direct.cxx',
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

executable(
  'RunYamlSubst',
  'RunYamlSubst.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    istream_dep,
  ])

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
  '../src/header_parser.cxx',
  include_directories: inc,
  dependencies: [
    putil_dep,
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
  '../src/client_balancer.cxx',
  '../src/balancer.cxx',
  '../src/bulldog.cxx',
  include_directories: inc,
  dependencies: [
    event_net_dep,
    stopwatch_dep,
    eutil_dep,
    raddress_dep,
  ])

executable('run_nfs_client',
  'run_nfs_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/pipe_stock.cxx',
  '../src/PipeLease.cxx',
  '../src/nfs/Istream.cxx',
  include_directories: inc,
  dependencies: [
    nfs_client_dep,
    istream_dep,
    stock_dep,
    io_dep,
  ])

test('t_cache', executable('t_cache',
  't_cache.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    eutil_dep,
    pool_dep,
  ]))

test('t_tcache', executable('t_tcache',
  't_tcache.cxx',
  '../src/PInstance.cxx',
  '../src/pexpand.cxx',
  '../src/widget/View.cxx',
  '../src/translation/Cache.cxx',
  '../src/translation/Transformation.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    libcommon_translation_dep,
    eutil_dep,
    raddress_dep,
  ]))

test('t_regex', executable('t_regex',
  't_regex.cxx',
  '../src/pexpand.cxx',
  '../src/regex.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    libpcre,
    putil_dep,
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
    libpcre,
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

test('t_stock', executable('t_stock',
  't_stock.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    stock_dep,
  ]))

test('t_resource_address', executable('t_resource_address',
  't_resource_address.cxx',
  't_http_address.cxx',
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
  '../src/balancer.cxx',
  '../src/address_list.cxx',
  '../src/bulldog.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    eutil_dep,
    pool_dep,
    net_dep,
  ]))

test('t_cgi', executable('t_cgi',
  't_cgi.cxx',
  '../src/PInstance.cxx',
  '../src/crash.cxx',
  '../src/direct.cxx',
  '../src/HttpResponseHandler.cxx',
  include_directories: inc,
  dependencies: [
    cgi_dep,
    system_dep,
    raddress_dep,
  ]),
  env: ['srcdir=' + meson.source_root()],
)

test('t_http_util', executable('t_http_util',
  't_http_util.cxx',
  include_directories: inc,
  dependencies: [
    http_util_dep,
  ]))

test('t_http_client', executable('t_http_client',
  't_http_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    http_client_dep,
    system_dep,
  ]),
  env: ['srcdir=' + meson.source_root()],
)

test('t_http_server', executable('t_http_server',
  't_http_server.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/address_string.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    http_server_dep,
    system_dep,
  ]))

test('t_fcgi_client', executable('t_fcgi_client',
  't_fcgi_client.cxx',
  'fcgi_server.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    fcgi_client_dep,
  ]),
)

test('t_was_client', executable('t_was_client',
  't_was_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    was_client_dep,
    declare_dependency(link_with: was_server),
  ]),
)

test('t_memcached_client', executable('t_memcached_client',
  't_memcached_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    memcached_client_dep,
  ]),
)

test('t_http_cache', executable('t_http_cache',
  't_http_cache.cxx',
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
  ]))

test('t_cookie_client', executable('t_cookie_client',
  't_cookie_client.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    cookie_dep,
  ]))

test('t_cookie_server', executable('t_cookie_server',
  't_cookie_server.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    cookie_dep,
  ]))

test('t_shm', executable('t_shm',
  't_shm.cxx',
  't_dpool.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    shm_dep,
  ]))

test('t_session', executable('t_session',
  't_session.cxx',
  'TestSessionId.cxx',
  '../src/crash.cxx',
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
    putil_dep,
  ]))

test('t_rewrite_uri', executable('t_rewrite_uri',
  't_rewrite_uri.cxx',
  'FailingResourceLoader.cxx',
  '../src/PInstance.cxx',
  '../src/penv.cxx',
  '../src/widget/RewriteUri.cxx',
  '../src/istream_escape.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    putil_dep,
    widget_dep,
    istream_dep,
  ]))

test('t_widget_registry', executable('t_widget_registry',
  't_widget_registry.cxx',
  '../src/PInstance.cxx',
  '../src/widget/Registry.cxx',
  include_directories: inc,
  dependencies: [
    widget_dep,
  ]))

test('t_widget_resolver', executable('t_widget_resolver',
  't_widget_resolver.cxx',
  '../src/widget/Resolver.cxx',
  include_directories: inc,
  dependencies: [
    widget_dep,
  ]))

test('t_wembed', executable('t_wembed',
  't_wembed.cxx',
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
  '../src/crash.cxx',
  '../src/HttpResponseHandler.cxx',
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
  't_uri_escape.cxx',
  't_uri_extract.cxx',
  't_uri_relative.cxx',
  't_uri_verify.cxx',
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
  '../src/direct.cxx',
  '../src/pipe_stock.cxx',
  '../src/PipeLease.cxx',
  include_directories: inc,
  dependencies: [
    eutil_dep,
    istream_dep,
  ])

t_istream_filter_deps = [
  istream_dep,
  declare_dependency(link_with: t_istream_filter),
]

test('t_istream_cat', executable('t_istream_cat',
  't_istream_cat.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_catch', executable('t_istream_catch',
  't_istream_catch.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_chunked', executable('t_istream_chunked',
  't_istream_chunked.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_dechunk', executable('t_istream_dechunk',
  't_istream_dechunk.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_dechunk_verbatim1', executable('t_istream_dechunk_verbatim1',
  't_istream_dechunk_verbatim.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_dechunk_verbatim2', executable('t_istream_dechunk_verbatim2',
  't_istream_dechunk_verbatim2.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_dechunk_verbatim3', executable('t_istream_dechunk_verbatim3',
  't_istream_dechunk_verbatim3.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('TestAutoPipeIstream', executable('TestAutoPipeIstream',
  'TestAutoPipeIstream.cxx',
  '../src/pipe_stock.cxx',
  '../src/PipeLease.cxx',
  include_directories: inc,
  dependencies: [stock_dep] + t_istream_filter_deps))

test('t_istream_hold', executable('t_istream_hold',
  't_istream_hold.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_delayed', executable('t_istream_delayed',
  't_istream_delayed.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_subst', executable('t_istream_subst',
  't_istream_subst.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_deflate', executable('t_istream_deflate',
  't_istream_deflate.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_byte', executable('t_istream_byte',
  't_istream_byte.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_replace', executable('t_istream_replace',
  't_istream_replace.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_replace2', executable('t_istream_replace2',
  't_istream_replace2.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_iconv', executable('t_istream_iconv',
  't_istream_iconv.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_html_escape', executable('t_istream_html_escape',
  't_istream_html_escape.cxx',
  '../src/istream_escape.cxx',
  '../src/istream_html_escape.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_fcgi', executable('t_istream_fcgi',
  't_istream_fcgi.cxx',
  '../src/fcgi/istream_fcgi.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_processor', executable('t_istream_processor',
  't_istream_processor.cxx',
  'FailingResourceLoader.cxx',
  '../src/crash.cxx',
  '../src/random.cxx',
  '../src/bp/Global.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/widget/Resolver.cxx',
  '../src/widget/RewriteUri.cxx',
  '../src/istream_escape.cxx',
  '../src/istream_html_escape.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    processor_dep,
    widget_dep,
    session_dep,
  ] + t_istream_filter_deps))

test('t_sink_header', executable('t_sink_header',
  't_sink_header.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_sink_header_empty', executable('t_sink_header_empty',
  't_sink_header_empty.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_istream_tee', executable('t_istream_tee',
  't_istream_tee.cxx',
  '../src/istream/sink_close.cxx',
  include_directories: inc,
  dependencies: t_istream_filter_deps))

test('t_growing_buffer', executable('t_growing_buffer',
  't_growing_buffer.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    t_istream_filter_deps,
  ]))

executable('RunNameCache',
  'RunNameCache.cxx',
  include_directories: inc,
  dependencies: [
    ssl_dep,
    pg_dep,
  ])

executable('RunAvahiExplorer',
  'RunAvahiExplorer.cxx',
  include_directories: inc,
  dependencies: [
    avahi_dep,
    net_dep,
    io_dep,
  ])
