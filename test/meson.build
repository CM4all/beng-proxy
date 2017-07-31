executable('TrafoExample',
  '../src/trafo/Server.cxx',
  '../src/trafo/Listener.cxx',
  '../src/trafo/Connection.cxx',
  '../src/trafo/AllocatedRequest.cxx',
  '../src/trafo/Response.cxx',
  'TrafoExample.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
    declare_dependency(link_with: event),
    declare_dependency(link_with: net),
    system_dep,
  ])

executable('dump_udp',
  'dump_udp.cxx',
  include_directories: inc,
  dependencies: [
    libdaemon,
    declare_dependency(link_with: net),
  ])

executable('dump_control',
  'dump_control.cxx',
  include_directories: inc,
  dependencies: [
    libdaemon,
    declare_dependency(link_with: control_server),
  ])

executable('send_control',
  'send_control.cxx',
  include_directories: inc,
  dependencies: [
    declare_dependency(link_with: control_server),
    declare_dependency(link_with: net),
  ])

executable('run_ua_parser',
  'run_ua_parser.cxx',
  '../src/ua_classification.cxx',
  include_directories: inc,
  dependencies: [
    declare_dependency(link_with: expand),
  ])

executable('run_parser_cdata',
  'run_parser_cdata.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    processor_dep,
  ])

executable('run_css_parser',
  'run_css_parser.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    processor_dep,
  ])

executable('run_client',
  'run_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/serialize.cxx',
  '../src/istream_gb.cxx',
  '../src/pipe_stock.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libhttp,
    declare_dependency(link_with: ssl),
    declare_dependency(link_with: ajp_client),
    declare_dependency(link_with: was_client),
    declare_dependency(link_with: http_client),
    declare_dependency(link_with: net),
    stock_dep,
    declare_dependency(link_with: socket),
    declare_dependency(link_with: thread_pool),
  ])

executable('run_delegate',
  'run_delegate.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: delegate_client),
    declare_dependency(link_with: spawn),
  ])

executable('run_http_cache',
  'run_http_cache.cxx',
  '../src/PInstance.cxx',
  '../src/istream_rubber.cxx',
  '../src/istream_unlock.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: http_cache),
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
    libhttp, libdaemon,
    declare_dependency(link_with: http_server),
    system_dep,
  ])

executable('run_memcached_client',
  'run_memcached_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/pipe_stock.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: memcached_client),
    stock_dep,
    declare_dependency(link_with: net),
    declare_dependency(link_with: io),
  ])

executable('dump_memcached_choice',
  'dump_memcached_choice.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/serialize.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: memcached_client),
    declare_dependency(link_with: net),
    declare_dependency(link_with: io),
    putil_dep,
  ])

executable('cleanup_memcached_choice',
  'cleanup_memcached_choice.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/serialize.cxx',
  '../src/tcp_stock.cxx',
  '../src/tcp_balancer.cxx',
  '../src/balancer.cxx',
  '../src/bulldog.cxx',
  '../src/failure.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
    declare_dependency(link_with: memcached_client),
    declare_dependency(link_with: net),
    declare_dependency(link_with: io),
    putil_dep,
    declare_dependency(link_with: raddress),
    stock_dep,
    declare_dependency(link_with: eutil),
    stopwatch_dep,
    declare_dependency(link_with: http_cache),
    declare_dependency(link_with: ssl),
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
    declare_dependency(link_with: net),
  ])

executable('run_was',
  'run_was.cxx',
  '../src/PInstance.cxx',
  '../src/pexpand.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
    declare_dependency(link_with: net),
    declare_dependency(link_with: was_client),
    stopwatch_dep,
  ])

executable('was_mirror',
  'was_mirror.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
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
    util_dep,
    time_dep,
    http_dep,
  ])

executable('request_translation',
  'request_translation.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: translation),
    widget_dep,
    stock_dep,
  ])

executable('run_subst',
  'run_subst.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: istream),
  ])

executable('run_cookie_client',
  'run_cookie_client.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libhttp,
    declare_dependency(link_with: cookie),
  ])

executable('run_cookie_server',
  'run_cookie_server.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libhttp,
    declare_dependency(link_with: cookie),
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
    libhttp,
    processor_dep,
    widget_dep,
    session_dep,
  ])

executable('run_client_balancer',
  'run_client_balancer.cxx',
  '../src/PInstance.cxx',
  '../src/client_balancer.cxx',
  '../src/balancer.cxx',
  '../src/failure.cxx',
  '../src/bulldog.cxx',
  include_directories: inc,
  dependencies: [
    libdaemon,
    declare_dependency(link_with: net),
    stopwatch_dep,
    declare_dependency(link_with: eutil),
    declare_dependency(link_with: raddress),
  ])

executable('run_nfs_client',
  'run_nfs_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/pipe_stock.cxx',
  '../src/nfs/Istream.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: nfs_client),
    declare_dependency(link_with: istream),
    stock_dep,
    io_dep,
  ])

test('t_cache', executable('t_cache',
  't_cache.cxx',
  '../src/PInstance.cxx',
  include_directories: inc,
  dependencies: [
    declare_dependency(link_with: eutil),
    pool_dep,
  ]))

test('t_tcache', executable('t_tcache',
  't_tcache.cxx',
  '../src/PInstance.cxx',
  '../src/pexpand.cxx',
  '../src/widget/View.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: translation),
  ]))

test('t_regex', executable('t_regex',
  't_regex.cxx',
  '../src/pexpand.cxx',
  '../src/regex.cxx',
  include_directories: inc,
  dependencies: [
    cppunit,
    libpcre,
    putil_dep,
  ]))

test('t_header_forward', executable('t_header_forward',
  't_header_forward.cxx',
  '../src/header_forward.cxx',
  '../src/header_copy.cxx',
  '../src/random.cxx',
  '../src/session_id.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    cppunit,
    libpcre,
    libdaemon, libhttp,
    putil_dep,
    http_dep,
    declare_dependency(link_with: cookie),
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
    libhttp,
    stock_dep,
  ]))

test('t_resource_address', executable('t_resource_address',
  't_resource_address.cxx',
  't_http_address.cxx',
  't_cgi_address.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    libhttp,
    declare_dependency(link_with: raddress),
  ]))

test('t_uri_verify', executable('t_uri_verify',
  't_uri_verify.cxx',
  include_directories: inc,
  dependencies: [
    util_dep,
  ]))

test('t_uri_relative', executable('t_uri_relative',
  't_uri_relative.cxx',
  include_directories: inc,
  dependencies: [
    putil_dep,
  ]))

test('t_expansible_buffer', executable('t_expansible_buffer',
  't_expansible_buffer.cxx',
  include_directories: inc,
  dependencies: [
    putil_dep,
  ]))

test('t_balancer', executable('t_balancer',
  't_balancer.cxx',
  '../src/balancer.cxx',
  '../src/failure.cxx',
  '../src/address_list.cxx',
  '../src/bulldog.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    libdaemon,
    declare_dependency(link_with: eutil),
    pool_dep,
    declare_dependency(link_with: net),
  ]))

test('t_cgi', executable('t_cgi',
  't_cgi.cxx',
  '../src/PInstance.cxx',
  '../src/crash.cxx',
  '../src/direct.cxx',
  '../src/http_response.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
    declare_dependency(link_with: cgi),
    system_dep,
    declare_dependency(link_with: raddress),
  ]),
  env: ['srcdir=' + meson.source_root()],
)

test('t_http_util', executable('t_http_util',
  't_http_util.cxx',
  include_directories: inc,
  dependencies: [
    declare_dependency(link_with: http_util),
  ]))

test('t_http_client', executable('t_http_client',
  't_http_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: http_client),
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
    libhttp,
    declare_dependency(link_with: http_server),
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
    libhttp,
    declare_dependency(link_with: fcgi_client),
  ]),
)

test('t_was_client', executable('t_was_client',
  't_was_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: was_client),
    declare_dependency(link_with: was_server),
  ]),
)

test('t_ajp_client', executable('t_ajp_client',
  't_ajp_client.cxx',
  'ajp_server.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/serialize.cxx',
  '../src/istream_gb.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: ajp_client),
  ]),
)

test('t_memcached_client', executable('t_memcached_client',
  't_memcached_client.cxx',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: memcached_client),
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
    libhttp,
    putil_dep,
    declare_dependency(link_with: http_cache),
  ]))

test('t_cookie_client', executable('t_cookie_client',
  't_cookie_client.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    declare_dependency(link_with: cookie),
  ]))

test('t_cookie_server', executable('t_cookie_server',
  't_cookie_server.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    declare_dependency(link_with: cookie),
  ]))

test('t_shm', executable('t_shm',
  't_shm.cxx',
  't_dpool.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    threads,
    declare_dependency(link_with: shm),
  ]))

test('t_session', executable('t_session',
  't_session.cxx',
  '../src/crash.cxx',
  '../src/random.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    session_dep,
  ]))

test('t_pool', executable('t_pool',
  't_pool.cxx',
  include_directories: inc,
  dependencies: [
    cppunit,
    pool_dep,
  ]))

test('t_rubber', executable('t_rubber',
  't_rubber.cxx',
  include_directories: inc,
  dependencies: [
    cppunit,
    memory_dep,
  ]))

test('t_sink_rubber', executable('t_sink_rubber',
  't_sink_rubber.cxx',
  '../src/sink_rubber.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    libhttp,
    declare_dependency(link_with: istream),
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
    libhttp, libdaemon,
    putil_dep,
    widget_dep,
    declare_dependency(link_with: istream),
  ]))

test('t_widget_registry', executable('t_widget_registry',
  't_widget_registry.cxx',
  '../src/PInstance.cxx',
  '../src/widget/Registry.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
    widget_dep,
  ]))

test('t_widget_resolver', executable('t_widget_resolver',
  't_widget_resolver.cxx',
  '../src/widget/Resolver.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
    widget_dep,
  ]))

test('t_wembed', executable('t_wembed',
  't_wembed.cxx',
  '../src/PInstance.cxx',
  '../src/widget/Inline.cxx',
  '../src/istream_html_escape.cxx',
  '../src/istream_escape.cxx',
  '../src/bp_global.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
    widget_dep,
    declare_dependency(link_with: istream),
    declare_dependency(link_with: http_util),
  ]))

test('t_widget_http', executable('t_widget_http',
  't_widget_http.cxx',
  '../src/PInstance.cxx',
  '../src/widget/Request.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/header_forward.cxx',
  '../src/resource_tag.cxx',
  '../src/random.cxx',
  '../src/crash.cxx',
  '../src/http_response.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libdaemon, libhttp,
    widget_dep,
    declare_dependency(link_with: istream),
    declare_dependency(link_with: http_util),
    session_dep,
    processor_dep,
    time_dep,
  ]))

test('t_uri', executable('t_uri',
  't_uri_escape.cxx',
  't_uri_extract.cxx',
  include_directories: inc,
  dependencies: [
    gtest,
    util_dep,
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
    threads,
    libhttp,
    processor_dep,
    widget_dep,
    session_dep,
  ]))

t_istream_filter = static_library('t_istream_filter',
  '../src/PInstance.cxx',
  '../src/direct.cxx',
  '../src/pipe_stock.cxx',
  include_directories: inc,
  dependencies: [
    libhttp,
    declare_dependency(link_with: eutil),
    declare_dependency(link_with: istream),
  ])

t_istream_filter_deps = [
  declare_dependency(link_with: istream),
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

test('t_istream_pipe', executable('t_istream_pipe',
  't_istream_pipe.cxx',
  '../src/pipe_stock.cxx',
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
  '../src/bp_global.cxx',
  '../src/widget/FromSession.cxx',
  '../src/widget/FromRequest.cxx',
  '../src/widget/Resolver.cxx',
  '../src/widget/RewriteUri.cxx',
  '../src/istream_escape.cxx',
  '../src/istream_html_escape.cxx',
  include_directories: inc,
  dependencies: [
    libhttp, libdaemon,
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
