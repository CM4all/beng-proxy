project('CM4all beng-proxy', 'cpp', version: '18.0.45',
  meson_version: '>= 0.56',
  default_options: [
    'cpp_std=c++2a',
    'warning_level=3',

    # This is only here to build subprojects as static libraries
    'default_library=static',

    'libwas:apreq2=disabled',
    'libwas:xios=disabled',
    'libwas:documentation=disabled',
  ],
)

version_h = configuration_data()
version_h.set_quoted('PACKAGE', meson.project_name())
version_h.set_quoted('VERSION', meson.project_version())
configure_file(output: 'version.h', configuration: version_h)

lb_features_config = configuration_data()
lb_features_config.set('ENABLE_CERTDB', get_option('certdb'))
configure_file(output: 'lb_features.h', configuration: lb_features_config)

compiler = meson.get_compiler('cpp')

debug = get_option('b_ndebug') != 'true'

common_flags = [
  '-D_REENTRANT', '-D_GNU_SOURCE',
]

test_common_flags = [
  '-Wcast-align',
  '-Wcast-qual',
  '-Wdouble-promotion',
  '-Wfloat-equal',
  '-Wmissing-declarations',
  '-Wmissing-noreturn',
  '-Wmissing-format-attribute',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wundef',
  '-Wunused',
  '-Wvla',
  '-Wwrite-strings',

  '-Wno-unused-result',

  # clang specific warning options:
  #'-Wunreachable-code-aggressive', (disabled, needs code fixes)
  #'-Wused-but-marked-unused', (disabled due to OpenSSL)
]

test_global_common_flags = [
  '-fvisibility=hidden',
]

add_global_link_arguments(
  compiler.get_supported_link_arguments(
    # make relocations read-only (hardening)
    '-Wl,-z,relro',

    # no lazy binding, please - not worth it for a daemon
    '-Wl,-z,now',
  ),
  language: 'cpp'
)

if not debug
  test_global_common_flags += [
    '-ffunction-sections',
    '-fdata-sections',
  ]

  add_global_link_arguments(
    compiler.get_supported_link_arguments(
      '-Wl,--gc-sections',
      '-Wl,--icf=all',
    ),
    language: 'cpp'
  )
endif

test_global_cxxflags = test_global_common_flags + [
]

test_cxxflags = test_common_flags + [
  '-fno-threadsafe-statics',
  '-fmerge-all-constants',

  '-Wcomma-subscript',
  '-Wextra-semi',
  '-Wmismatched-tags',
  '-Woverloaded-virtual',
  '-Wsign-promo',
  '-Wvolatile',
  '-Wvirtual-inheritance',

  '-Wno-missing-field-initializers',

  # a vtable without a dtor is just fine
  '-Wno-non-virtual-dtor',

  # the only warnings we got from this are from formatted error
  # messages, and their truncation is harmless
  '-Wno-format-truncation',

  # clang specific warning options:
  '-Wcomma',
  '-Wheader-hygiene',
  '-Winconsistent-missing-destructor-override',
]

if compiler.get_id() == 'gcc' and compiler.version().version_compare('>=11')
  # too many bogus warnings, e.g. for std::array::size()
  test_cxxflags += '-Wno-maybe-uninitialized'
endif

add_global_arguments(common_flags, language: 'cpp')
add_global_arguments(compiler.get_supported_arguments(test_global_cxxflags), language: 'cpp')
add_project_arguments(compiler.get_supported_arguments(test_cxxflags), language: 'cpp')

if debug
  debug_flags = ['-DPOISON', '-DENABLE_TRACE']
  debug_flags += ['-DDEBUG_POOL_REF']
else
  debug_flags = []
endif

stopwatch = get_option('stopwatch')
if stopwatch
  debug_flags += ['-DENABLE_STOPWATCH']
endif

if debug and compiler.has_header('valgrind/memcheck.h')
  debug_flags += '-DHAVE_VALGRIND_MEMCHECK_H'
endif

add_project_arguments(debug_flags, language: 'c')
add_project_arguments(debug_flags, language: 'cpp')

threads = dependency('threads')
zlib = dependency('zlib')
libcrypt = compiler.find_library('crypt')

inc = include_directories(
  'src',
  'libcommon/src',
  '.',
)

libcommon_require_cap = get_option('cap')
libcommon_require_lua = get_option('lua')

subdir('libcommon/src/util')
subdir('libcommon/src/lib/cap')
subdir('libcommon/src/lib/fmt')
subdir('libcommon/src/lib/nlohmann_json')
subdir('libcommon/src/uri')

util2 = static_library(
  'util2',
  'src/util/LimitedConcurrencyQueue.cxx',
  'src/util/StringList.cxx',
  'src/util/StringSet.cxx',
  'src/uri/Base.cxx',
  'src/uri/Compare.cxx',
  'src/uri/Dissect.cxx',
  'src/uri/Recompose.cxx',
  'src/uri/Relative.cxx',
  'src/uri/RedirectHttps.cxx',
  'src/escape/HTML.cxx',
  'src/escape/CSS.cxx',
  include_directories: inc,
)
util_dep = declare_dependency(
  link_with: util2,
  dependencies: [
    util_dep,
    uri_dep,
  ],
)

subdir('libcommon/src/co')

subdir('libcommon/src/lib/pcre')
subdir('libcommon/src/lib/sodium')
subdir('libcommon/src/time')
subdir('libcommon/src/io')
subdir('libcommon/src/io/linux')
subdir('libcommon/src/io/config')

io2 = static_library(
  'io2',
  'src/io/Buffered.cxx',
  'src/io/SpliceSupport.cxx',
  include_directories: inc,
)
io_dep = declare_dependency(
  link_with: io2,
  dependencies: [
    io_dep,
  ],
)

libcommon_require_uring = get_option('io_uring')
subdir('libcommon/src/io/uring')
subdir('libcommon/src/system')

system2 = static_library(
  'system2',
  'src/system/SetupProcess.cxx',
  'src/system/Isolate.cxx',
  'src/system/Capabilities.cxx',
  include_directories: inc,
  dependencies: [
    threads,
  ],
)
system_dep = declare_dependency(
  link_with: system2,
  dependencies: [
    system_dep,
  ],
)

subdir('libcommon/src/http')
subdir('libcommon/src/lua')

subdir('src/memory')
subdir('src/prometheus')

pool = static_library('pool',
  'src/AllocatorPtr.cxx',
  'src/pool/Ptr.cxx',
  'src/pool/RootPool.cxx',
  'src/pool/tpool.cxx',
  'src/pool/pstring.cxx',
  'src/pool/pool.cxx',
  'src/pool/LeakDetector.cxx',
  include_directories: inc,
)
pool_dep = declare_dependency(link_with: pool,
                             dependencies: [memory_dep, io_dep])

putil = static_library('putil',
  'src/strmap.cxx',
  'src/PStringSet.cxx',
  'src/uri/PEdit.cxx',
  'src/uri/PEscape.cxx',
  'src/uri/PRelative.cxx',
  'src/escape/Pool.cxx',
  include_directories: inc,
  dependencies: [
  ])
putil_dep = declare_dependency(link_with: putil,
  dependencies: [
    pool_dep,
  ],
)

subdir('libcommon/src/event')

# Utility library using libevent
eutil = static_library('eutil',
  'src/cache.cxx',
  include_directories: inc,
)
eutil_dep = declare_dependency(link_with: eutil,
                              dependencies: [event_dep])

subdir('libcommon/src/net')
subdir('libcommon/src/net/control')
subdir('libcommon/src/net/log')

net2 = static_library(
  'net2',
  'src/net/FailureManager.cxx',
  'src/net/FailureInfo.cxx',
  'src/net/FailureRef.cxx',
  'src/net/TempListener.cxx',
  'src/net/ClientAccounting.cxx',
  include_directories: inc,
)
net_dep = declare_dependency(
  link_with: net2,
  dependencies: [
    net_dep,
  ],
)

subdir('libcommon/src/event/net')
subdir('libcommon/src/event/net/control')
subdir('libcommon/src/event/net/log')
subdir('libcommon/src/event/uring')

event_net2 = static_library(
  'event_net2',
  'src/net/PConnectSocket.cxx',
  'src/net/Ping.cxx',
  include_directories: inc,
)
event_net_dep = declare_dependency(
  link_with: event_net2,
  dependencies: [
    event_net_dep,
    memory_dep,
  ],
)

if stopwatch
  stopwatch = static_library('stopwatch',
    'src/istream_stopwatch.cxx',
    'src/stopwatch.cxx',
    include_directories: inc,
  )
  stopwatch_dep = declare_dependency(link_with: stopwatch,
                                    dependencies: [net_dep])
else
  stopwatch_dep = declare_dependency()
endif

libcommon_enable_was = get_option('was')
libcommon_enable_seccomp = get_option('seccomp')
libcommon_enable_libsystemd = dependency('libsystemd', required: get_option('systemd')).found()

subdir('libcommon/src/was/async')
subdir('libcommon/src/stock')

stock2 = static_library(
  'stock2',
  'src/stock/Lease.cxx',
  include_directories: inc,
)
stock_dep = declare_dependency(
  link_with: stock2,
  dependencies: [stock_dep, event_dep],
)

openssl_min_version = '3'
openssl_api_compat = '0x30000000L'

subdir('libcommon/src/lib/openssl')
subdir('libcommon/src/jwt')

ssl2_sources = []

if get_option('certdb')
  subdir('libcommon/src/pg')

  ssl2_sources += [
    '../certdb/CertDatabase.cxx',
    '../certdb/CoCertDatabase.cxx',
    '../certdb/FromResult.cxx',
    '../certdb/WrapKey.cxx',
    '../certdb/Wildcard.cxx',
    'Cache.cxx',
    'NameCache.cxx',
    'DbCertCallback.cxx',
  ]
else
  pg_dep = dependency('', required: false)
endif

subdir('src/ssl')
subdir('src/control')

libcommon_require_avahi = get_option('zeroconf')
subdir('libcommon/src/lib/avahi')

if get_option('certdb')
  subdir('libcommon/src/lib/curl')
endif

if libcommon_enable_libsystemd or avahi_dep.found()
  subdir('libcommon/src/lib/dbus')
else
  odbus_dep = dependency('', required: false)
endif

subdir('src/http')
subdir('src/pipe')
subdir('src/istream')

memory_istream = static_library(
  'memory_istream',
  'src/memory/istream_gb.cxx',
  'src/memory/SinkGrowingBuffer.cxx',
  'src/memory/istream_rubber.cxx',
  'src/memory/sink_rubber.cxx',
  include_directories: inc,
  dependencies: [
    memory_dep,
    istream_api_dep,
  ],
)

memory_istream_dep = declare_dependency(
  link_with: memory_istream,
  dependencies: [memory_dep, istream_api_dep],
)

expand = static_library('expand',
  'src/regex.cxx',
  'src/pexpand.cxx',
  include_directories: inc,
  dependencies: [
    pcre_dep,
  ])
expand_dep = declare_dependency(link_with: expand,
                                dependencies: [putil_dep])

subdir('libcommon/src/adata')
subdir('libcommon/src/spawn')

spawn_dep = declare_dependency(
  dependencies: [
    spawn_dep,
    expand_dep,
  ],
)

istream_spawn = static_library(
  'istream_spawn',
  'src/spawn/IstreamSpawn.cxx',
  'src/spawn/ChildStock.cxx',
  'src/spawn/ChildStockItem.cxx',
  'src/spawn/ListenChildStock.cxx',
  include_directories: inc,
)
istream_spawn_dep = declare_dependency(
  link_with: istream_spawn,
  dependencies: [
    spawn_dep,
    istream_dep,
  ],
)

subdir('src/access_log')
subdir('src/cluster')

raddress = static_library('raddress',
  'src/regex.cxx',
  'src/cgi/ChildParams.cxx',
  'src/cgi/Address.cxx',
  'src/file/Address.cxx',
  'src/delegate/Address.cxx',
  'src/nfs/Address.cxx',
  'src/http/local/Address.cxx',
  'src/http/Address.cxx',
  'src/cluster/AddressList.cxx',
  'src/cluster/AddressListBuilder.cxx',
  'src/ResourceAddress.cxx',
  include_directories: inc,
  dependencies: [
    libpcre,
    sodium_dep,
  ])
raddress_dep = declare_dependency(
  link_with: raddress,
  dependencies: [
    net_dep,
    spawn_dep,
    adata_dep,
    expand_dep,
    pcre_dep,
  ],
)

subdir('src/was')
subdir('src/thread')
subdir('src/fs')

subdir('libcommon/src/translation')
subdir('libcommon/src/translation/server')

libcommon_translation_dep = translation_dep


translation2 = static_library(
  'translation2',
  'src/translation/Builder.cxx',
  'src/translation/Multi.cxx',
  'src/translation/Cache.cxx',
  'src/translation/Stock.cxx',
  'src/translation/Layout.cxx',
  'src/translation/Marshal.cxx',
  'src/translation/Client.cxx',
  'src/translation/Transformation.cxx',
  'src/translation/FilterTransformation.cxx',
  'src/translation/SubstTransformation.cxx',
  'src/translation/Vary.cxx',
  'src/translation/InvalidateParser.cxx',
  'src/translation/CoTranslate.cxx',
  include_directories: inc,
  dependencies: [
    coroutines_dep,
    fmt_dep,
    pcre_dep,
  ],
)

translation_dep = declare_dependency(
  link_with: translation2,
  dependencies: [
    coroutines_dep,
    translation_dep,
    event_dep,
    putil_dep,
    eutil_dep,
    raddress_dep,
    socket_dep,
    stopwatch_dep
  ],
)

http_common = static_library('http_common',
  'src/http/Body.cxx',
  include_directories: inc,
)
http_common_dep = declare_dependency(link_with: http_common,
                                    dependencies: [istream_dep, io_dep])

http_client = static_library('http_client',
  'src/http/Client.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)
http_client_dep = declare_dependency(
  link_with: http_client,
  dependencies: [
    socket_dep,
    putil_dep,
    http_util_dep,
    http_common_dep,
    memory_istream_dep,
    stopwatch_dep,
  ],
)

subdir('src/http/server')
subdir('src/nghttp2')
subdir('src/http/cache')
subdir('src/cgi')
subdir('src/fcgi')
subdir('src/nfs')
subdir('src/delegate')
subdir('src/bp/session')
subdir('src/widget')

processor = static_library('processor',
  'src/parser/XmlParser.cxx',
  'src/parser/CssParser.cxx',
  'src/bp/WidgetContainerParser.cxx',
  'src/bp/WidgetLookupProcessor.cxx',
  'src/bp/XmlProcessor.cxx',
  'src/bp/ProcessorHeaders.cxx',
  'src/bp/CssProcessor.cxx',
  'src/bp/CssRewrite.cxx',
  'src/bp/TextProcessor.cxx',
  'src/bp/ClassifyMimeType.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
  ],
)
processor_dep = declare_dependency(
  link_with: processor,
  dependencies: [
    istream_dep,
    putil_dep,
    stopwatch_dep,
  ],
)

sources = [
]

if uring_dep.found()
  sources += [
    'src/io/UringOpen.cxx',
    'src/io/UringOpenStat.cxx',
    'src/io/UringStat.cxx',
  ]
endif

if nfs_client_dep.found()
  sources += [
    'src/nfs/RequestHandler.cxx',
    'src/nfs/Istream.cxx',
  ]
endif

executable(
  'cm4all-beng-proxy',
  sources,
  'src/io/FdCache.cxx',
  'src/delegate/RequestHandler.cxx',
  'src/address_string.cxx',
  'src/cluster/TcpBalancer.cxx',
  'src/bp/Bot.cxx',
  'libcommon/src/pg/Interval.cxx',
  'src/bp/CommandLine.cxx',
  'src/bp/Config.cxx',
  'src/bp/ConfigParser.cxx',
  'src/bp/Listener.cxx',
  'src/bp/Connection.cxx',
  'src/bp/RLogger.cxx',
  'src/bp/drop.cxx',
  'src/uri/Relocate.cxx',
  'src/http/cache/FilterCache.cxx',
  'src/http/cache/EncodingCache.cxx',
  'src/bp/FileHeaders.cxx',
  'src/bp/FileHandler.cxx',
  'src/bp/EmulateModAuthEasy.cxx',
  'src/bp/AprMd5.cxx',
  'src/bp/ProxyHandler.cxx',
  'src/bp/Global.cxx',
  'src/bp/Base.cxx',
  'src/bp/Handler.cxx',
  'src/bp/CoHandler.cxx',
  'src/bp/Auth.cxx',
  'src/bp/HttpAuth.cxx',
  'src/bp/TokenAuth.cxx',
  'src/load_file.cxx',
  'src/bp/file_not_found.cxx',
  'src/bp/file_enotdir.cxx',
  'src/bp/file_directory_index.cxx',
  'src/bp/ForwardHeaders.cxx',
  'src/http/AnyClient.cxx',
  'src/http/GlueClient.cxx',
  'src/http/ResponseHandler.cxx',
  'src/http/CoResponseHandler.cxx',
  'src/bp/Stats.cxx',
  'src/bp/Control.cxx',
  'src/pipe_filter.cxx',
  'src/bp/ProxyWidget.cxx',
  'src/bp/ForwardRequest.cxx',
  'src/bp/Request.cxx',
  'src/bp/RSession.cxx',
  'src/bp/ExternalSession.cxx',
  'src/bp/CollectCookies.cxx',
  'src/bp/CsrfProtection.cxx',
  'src/bp/CsrfToken.cxx',
  'src/bp/RError.cxx',
  'src/bp/Response.cxx',
  'src/bp/GenerateResponse.cxx',
  'src/bp/PrometheusExporter.cxx',
  'src/resource_tag.cxx',
  'src/widget/RewriteUri.cxx',
  'src/file/Request.cxx',
  'src/file/Headers.cxx',
  'src/translation/SuffixRegistry.cxx',
  'src/translation/AddressSuffixRegistry.cxx',
  'src/tcp_stock.cxx',
  'src/http/local/Stock.cxx',
  'src/http/local/Glue.cxx',
  'src/bp/errdoc.cxx',
  'src/widget/FromRequest.cxx',
  'src/widget/FromSession.cxx',
  'src/widget/Registry.cxx',
  'src/widget/Resolver.cxx',
  'src/widget/Request.cxx',
  'src/widget/Inline.cxx',
  'src/escape/Istream.cxx',
  'src/istream_html_escape.cxx',
  'src/ssl/SslSocketFilterFactory.cxx',
  'src/DirectResourceLoader.cxx',
  'src/CachedResourceLoader.cxx',
  'src/FilterResourceLoader.cxx',
  'src/BufferedResourceLoader.cxx',
  'src/widget/Frame.cxx',
  'src/access_log/ChildErrorLog.cxx',
  'src/PInstance.cxx',
  'src/bp/UringGlue.cxx',
  'src/bp/Instance.cxx',
  'src/bp/Main.cxx',
  include_directories: inc,
  dependencies: [
    fmt_dep,
    memory_istream_dep,
    istream_pipe_dep,
    istream_extra_dep,
    access_log_client_dep,
    event_uring_dep,
    event_net_log_dep,
    avahi_dep,
    odbus_dep,
    pool_dep,
    io_config_dep,
    net_dep,
    raddress_dep,
    spawn_dep,
    http_server_dep,
    http_client_dep,
    http_cache_dep,
    ssl_dep,
    translation_dep,
    was_stock_dep,
    stopwatch_dep,
    cgi_dep,
    fcgi_stock_dep,
    session_dep,
    widget_dep,
    processor_dep,
    nfs_client_dep,
    delegate_client_dep,
    control_server_dep,
    nghttp2_client_dep,
    nghttp2_server_dep,
    cluster_dep,
    sodium_dep,
    prometheus_dep,
    libcrypt,
  ],
  install: true,
  install_dir: 'sbin',
)

lb_config_sources = []
if avahi_dep.found()
  lb_config_sources += 'src/lb/ZeroconfDiscoveryConfig.cxx'
endif

lb_config = static_library(
  'lb_config',
  lb_config_sources,
  'src/lb/Config.cxx',
  'src/lb/ConfigParser.cxx',
  'src/lb/GotoConfig.cxx',
  'src/lb/ClusterConfig.cxx',
  include_directories: inc,
  dependencies: [
    avahi_dep, # only for the HAVE_AVAHI macro
    fmt_dep,
    nghttp2_features_dep, # only for the HAVE_NGHTTP2 macro
    io_config_dep,
    libpcre,
  ],
)

lb_config_dep = declare_dependency(
  link_with: lb_config,
  dependencies: [
    avahi_dep, # only for the HAVE_AVAHI macro
    nghttp2_features_dep, # only for the HAVE_NGHTTP2 macro
    libpcre,
  ],
)

lb_sources = []
if avahi_dep.found()
  lb_sources += 'src/lb/PrometheusDiscovery.cxx'
endif

if lua_dep.found()
  lb_sources += [
    'src/lb/LuaHandler.cxx',
    'src/lb/LuaInitHook.cxx',
    'src/lb/LuaGoto.cxx',
    'src/lb/LuaHttpRequestHandler.cxx',
  ]
endif

executable(
  'cm4all-beng-lb',
  lb_sources,
  'src/tcp_stock.cxx',
  'src/address_string.cxx',
  'src/cluster/AddressList.cxx',
  'src/cluster/ConnectBalancer.cxx',
  'src/cluster/TcpBalancer.cxx',
  'src/http/GlueClient.cxx',
  'src/http/Address.cxx',
  'src/http/ResponseHandler.cxx',
  'src/PInstance.cxx',
  'src/certdb/Config.cxx',
  'src/lb/Context.cxx',
  'src/lb/Listener.cxx',
  'src/lb/SimpleHttpResponse.cxx',
  'src/lb/HttpConnection.cxx',
  'src/lb/RLogger.cxx',
  'src/lb/ResolveConnect.cxx',
  'src/lb/TranslationHttpRequestHandler.cxx',
  'src/lb/TcpConnection.cxx',
  'src/lb/ForwardHttpRequest.cxx',
  'src/lb/DelayForwardHttpRequest.cxx',
  'src/lb/Stats.cxx',
  'src/lb/Control.cxx',
  'src/lb/JvmRoute.cxx',
  'src/lb/Headers.cxx',
  'src/lb/Session.cxx',
  'src/lb/Check.cxx',
  'src/lb/Cookie.cxx',
  'src/lb/CommandLine.cxx',
  'src/lb/ConditionConfig.cxx',
  'src/lb/lb_check.cxx',
  'src/lb/Setup.cxx',
  'src/lb/GotoMap.cxx',
  'src/lb/Branch.cxx',
  'src/lb/MemberHash.cxx',
  'src/lb/Cluster.cxx',
  'src/lb/TranslationHandler.cxx',
  'src/lb/TranslationCache.cxx',
  'src/lb/MonitorController.cxx',
  'src/lb/MonitorRef.cxx',
  'src/lb/MonitorStock.cxx',
  'src/lb/MonitorManager.cxx',
  'src/lb/PingMonitor.cxx',
  'src/lb/PrometheusExporter.cxx',
  'src/lb/SynMonitor.cxx',
  'src/lb/ExpectMonitor.cxx',
  'src/lb/Instance.cxx',
  'src/lb/Main.cxx',

  'src/ssl/SslSocketFilterFactory.cxx',

  # the following sources are only necessary for the translation client
  'src/widget/View.cxx',
  'src/widget/VList.cxx',

  include_directories: inc,
  dependencies: [
    fmt_dep,
    lb_config_dep,
    pipe_dep,
    istream_extra_dep,
    memory_istream_dep,
    access_log_client_dep,
    liblua,
    libpcre,
    sodium_dep,
    avahi_dep,
    pool_dep,
    io_dep,
    io_config_dep,
    socket_dep,
    event_dep,
    net_dep,
    net_log_dep,
    ssl_dep,
    pg_dep,
    nghttp2_server_dep,
    nghttp2_client_dep,
    http_server_dep,
    http_client_dep,
    putil_dep,
    eutil_dep,
    stock_dep,
    translation_dep,
    prometheus_dep,
    spawn_dep,
    expand_dep,
    stopwatch_dep,
    cookie_dep,
    control_server_dep,
    odbus_dep,
    lua_dep,
    pcre_dep,
    cluster_dep,
  ],
  install: true,
  install_dir: 'sbin',
)

if get_option('certdb')
  executable(
    'cm4all-certdb',
    'src/certdb/Config.cxx',
    'src/certdb/Progress.cxx',
    'src/certdb/WrapKey.cxx',
    'src/certdb/CertDatabase.cxx',
    'src/certdb/Migrate.cxx',
    'src/certdb/FromResult.cxx',
    'src/certdb/Wildcard.cxx',
    'src/certdb/GlueHttpClient.cxx',
    'src/certdb/JWS.cxx',
    'src/certdb/AcmeError.cxx',
    'src/certdb/AcmeHttp.cxx',
    'src/certdb/AcmeDns.cxx',
    'src/certdb/AcmeAlpn.cxx',
    'src/certdb/AcmeClient.cxx',
    'src/certdb/AcmeChallenge.cxx',
    'src/certdb/AcmeAccount.cxx',
    'src/certdb/AcmeOrder.cxx',
    'src/certdb/AcmeAuthorization.cxx',
    'src/certdb/AcmeKey.cxx',
    'src/certdb/FakeAcmeClient.cxx',
    'src/certdb/AcmeMain.cxx',
    'src/certdb/Main.cxx',
    'src/access_log/ConfigParser.cxx',
    include_directories: inc,
    dependencies: [
      lb_config_dep,
      nlohmann_json_dep,
      fmt_dep,
      pcre_dep,
      sodium_dep,
      crypto_dep,
      pg_dep,
      curl_dep,
      io_config_dep,
      net_dep,
      http_dep,
      io_dep,
      jwt_dep,
    ],
    install: true,
  )
endif

executable(
  'delegate-helper',
  'src/delegate/Helper.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
    util_dep,
  ],
  install: true,
  install_dir: 'lib/cm4all/beng-proxy/delegate/bin',
)

conf = configuration_data()
conf.set('HAVE_AVAHI', avahi_dep.found())
conf.set('HAVE_LIBCAP', cap_dep.found())
conf.set('HAVE_LIBSYSTEMD', libsystemd.found())
conf.set('HAVE_LUA', lua_dep.found())
configure_file(output: 'config.h', configuration: conf)

subdir('doc')
subdir('test')
subdir('libcommon/test/util')
subdir('libcommon/test/uri')
subdir('libcommon/test/http')
subdir('libcommon/test/io/config')
if get_option('certdb')
  subdir('libcommon/test/pg')
endif
subdir('libcommon/test/sodium')
subdir('libcommon/test/stock')
