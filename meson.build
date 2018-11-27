project('CM4all beng-proxy', ['c', 'cpp'], version: '15.0.10',
  meson_version: '>= 0.47',
  default_options: [
    'c_std=c99',
    'cpp_std=c++14'
  ],
)

compiler = meson.get_compiler('cpp')
c_compiler = meson.get_compiler('c')

pkg_mod = import('pkgconfig')
pkg_mod.generate(version: meson.project_version(),
                 name: 'libcm4all-beng-proxy3',
                 description: 'CM4all BENG proxy library')

# TODO: use get_option('b_ndebug'), but that doesn't work with Meson 0.37.1
if get_option('buildtype') == 'debug'
  debug = true
else
  debug = false
endif

common_flags = [
  '-D_REENTRANT', '-D_GNU_SOURCE',
  '-DSPLICE',
  '-DPACKAGE="' + meson.project_name() + '"',
  '-DVERSION="' + meson.project_version() + '"',
  '-DCACHE_LOG',
  '-Wall',
  '-Wextra',
  '-Wwrite-strings', '-Wcast-qual', '-Wcast-align', '-Wfloat-equal',
  '-Wshadow', '-Wpointer-arith', '-Wsign-compare',
  '-Wmissing-declarations', '-Wmissing-noreturn', '-Wmissing-format-attribute',
  '-Wredundant-decls', '-Wno-long-long', '-Wundef',
  '-Wunused',
  '-Wundef',
]

test_common_flags = [
  '-fno-omit-frame-pointer',
  '-fvisibility=hidden',
]

if not debug
  test_common_flags += [
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

test_cxxflags = test_common_flags + [
  '-fno-threadsafe-statics',
  '-fmerge-all-constants',
  '-Wno-variadic-macros',
  '-Wno-missing-field-initializers',
  '-Wno-non-virtual-dtor',

  # work around bogus GCC7 warning "mangled name for ... will change
  # in C++17 because the exception specification is part of a function
  # type"
  '-Wno-noexcept-type',

  # the only warnings we got from this are from formatted error
  # messages, and their truncation is harmless
  '-Wno-format-truncation',
]

test_cflags = test_common_flags + [
  '-Wmissing-prototypes', '-Wstrict-prototypes',
  '-Wbad-function-cast',
  '-Waggregate-return',
  '-Wnested-externs',
  '-pedantic',
]

add_global_arguments(common_flags, language: 'c')
add_global_arguments(common_flags, language: 'cpp')

add_global_arguments(compiler.get_supported_arguments(test_cxxflags), language: 'cpp')
add_global_arguments(c_compiler.get_supported_arguments(test_cflags), language: 'c')

if debug
  debug_flags = ['-DPOISON', '-DTRACE']
  debug_flags += ['-DDEBUG_POOL_REF']
else
  debug_flags = []
endif

stopwatch = debug
if stopwatch
  debug_flags += ['-DENABLE_STOPWATCH']
endif

if compiler.has_header('valgrind/memcheck.h')
  add_global_arguments('-DHAVE_VALGRIND_MEMCHECK_H', language: 'cpp')
  add_global_arguments('-DHAVE_VALGRIND_MEMCHECK_H', language: 'c')

  if debug
    debug_flags += ['-DVALGRIND']
  endif
endif

add_global_arguments(debug_flags, language: 'c')
add_global_arguments(debug_flags, language: 'cpp')

threads = dependency('threads')
libpcre = dependency('libpcre')
libsodium = dependency('libsodium')
libjsoncpp = dependency('jsoncpp')
libyamlcpp = dependency('yaml-cpp')
libnfs = dependency('libnfs')
zlib = dependency('zlib')

gtest_compile_args = [
  '-Wno-undef',
]

if compiler.get_id() == 'gcc'
  gtest_compile_args += [
    '-Wno-suggest-attribute=format',
    '-Wno-suggest-attribute=noreturn',
    '-Wno-missing-declarations',

    # needed on Jessie for gtest's IsNullLiteralHelper
    '-Wno-conversion-null',
  ]
endif

gtest = declare_dependency(
  dependencies: [dependency('gtest', main: true)],
  compile_args: gtest_compile_args,
)

libwas = dependency('libcm4all-was-protocol', version: '>= 1.0')

inc = include_directories('src', 'include', 'libcommon/src')

subdir('libcommon/src/util')

util2 = static_library(
  'util2',
  'src/util/StringSet.cxx',
  'src/uri/uri_base.cxx',
  'src/uri/Compare.cxx',
  'src/uri/Dissect.cxx',
  'src/uri/uri_verify.cxx',
  'src/uri/uri_escape.cxx',
  'src/uri/uri_extract.cxx',
  'src/uri/uri_relative.cxx',
  'src/escape_html.cxx',
  'src/escape_css.cxx',
  include_directories: inc,
)
util_dep = declare_dependency(
  link_with: util2,
  dependencies: [
    util_dep,
  ],
)

subdir('libcommon/src/time')
subdir('libcommon/src/io')

io2 = static_library(
  'io2',
  'src/io/Buffered.cxx',
  'src/io/StringFile.cxx',
  include_directories: inc,
)
io_dep = declare_dependency(
  link_with: io2,
  dependencies: [
    io_dep,
  ],
)

subdir('libcommon/src/system')

system2 = static_library(
  'system2',
  'src/system/SetupProcess.cxx',
  'src/system/Isolate.cxx',
  'src/system/urandom.cxx',
  include_directories: inc,
)
system_dep = declare_dependency(
  link_with: system2,
  dependencies: [
    system_dep,
  ],
)

subdir('libcommon/src/http')
subdir('libcommon/src/lua')

memory = static_library('memory',
  'src/fb_pool.cxx',
  'src/SlicePool.cxx',
  'src/SliceAllocation.cxx',
  'src/SliceFifoBuffer.cxx',
  'src/DefaultFifoBuffer.cxx',
  'src/DefaultChunkAllocator.cxx',
  'src/GrowingBuffer.cxx',
  'src/rubber.cxx',
  include_directories: inc,
  dependencies: [
  ])
memory_dep = declare_dependency(link_with: memory,
                               dependencies: [system_dep])

pool = static_library('pool',
  'src/AllocatorPtr.cxx',
  'src/pool/Ptr.cxx',
  'src/pool/RootPool.cxx',
  'src/pool/tpool.cxx',
  'src/pool/pbuffer.cxx',
  'src/pool/pstring.cxx',
  'src/pool/pool.cxx',
  'src/pool/LeakDetector.cxx',
  include_directories: inc,
)
pool_dep = declare_dependency(link_with: pool,
                             dependencies: [memory_dep, io_dep])

putil = static_library('putil',
  'src/expansible_buffer.cxx',
  'src/strmap.cxx',
  'src/PStringSet.cxx',
  'src/puri_edit.cxx',
  'src/puri_escape.cxx',
  'src/puri_relative.cxx',
  'src/escape_pool.cxx',
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
  'src/notify.cxx',
  'src/cache.cxx',
  include_directories: inc,
)
eutil_dep = declare_dependency(link_with: eutil,
                              dependencies: [event_dep])

subdir('libcommon/src/net')

net2 = static_library(
  'net2',
  'src/net/FailureManager.cxx',
  'src/net/FailureInfo.cxx',
  'src/net/FailureRef.cxx',
  include_directories: inc,
)
net_dep = declare_dependency(
  link_with: net2,
  dependencies: [
    net_dep,
  ],
)

subdir('libcommon/src/event/net')

event_net2 = static_library(
  'event_net2',
  'src/net/PConnectSocket.cxx',
  'src/net/Ping.cxx',
  'src/net/UdpDistribute.cxx',
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

stock = static_library('stock',
  'src/stock/Item.cxx',
  'src/stock/Stock.cxx',
  'src/stock/MapStock.cxx',
  'src/stock/MultiStock.cxx',
  'src/stock/Lease.cxx',
  include_directories: inc,
)
stock_dep = declare_dependency(link_with: stock,
                               dependencies: [event_dep, pool_dep, io_dep])

shm = static_library('shm',
  'src/shm/dpool.cxx',
  'src/shm/dstring.cxx',
  'src/shm/String.cxx',
  'src/shm/shm.cxx',
  include_directories: inc,
  dependencies: [
    threads,
  ])
shm_dep = declare_dependency(link_with: shm,
                             dependencies: [io_dep])

subdir('libcommon/src/ssl')

ssl2 = static_library(
  'ssl2',
  'src/certdb/CertDatabase.cxx',
  'src/certdb/FromResult.cxx',
  'src/certdb/WrapKey.cxx',
  'src/certdb/Wildcard.cxx',
  'src/ssl/Basic.cxx',
  'src/ssl/Cache.cxx',
  'src/ssl/NameCache.cxx',
  'src/ssl/Client.cxx',
  'src/ssl/Factory.cxx',
  'src/ssl/DbSniCallback.cxx',
  'src/ssl/FifoBufferBio.cxx',
  'src/ssl/Filter.cxx',
  'src/ssl/Init.cxx',
  include_directories: inc,
)
ssl_dep = declare_dependency(
  link_with: ssl2,
  dependencies: [
    ssl_dep,
  ],
)

subdir('libcommon/src/avahi')
subdir('libcommon/src/pg')
subdir('libcommon/src/curl')
subdir('libcommon/src/odbus')

istream = static_library('istream',
  'src/istream/Invoke.cxx',
  'src/istream/Pointer.cxx',
  'src/istream/UnusedPtr.cxx',
  'src/istream/TimeoutIstream.cxx',
  'src/istream/istream_pause.cxx',
  'src/istream/istream.cxx',
  'src/istream/istream_memory.cxx',
  'src/istream/MemoryIstream.cxx',
  'src/istream/SliceIstream.cxx',
  'src/istream/istream_null.cxx',
  'src/istream/ZeroIstream.cxx',
  'src/istream/BlockIstream.cxx',
  'src/istream/istream_string.cxx',
  'src/istream/FileIstream.cxx',
  'src/istream/PipeLeaseIstream.cxx',
  'src/istream/istream_chunked.cxx',
  'src/istream/istream_dechunk.cxx',
  'src/istream/ConcatIstream.cxx',
  'src/istream/AutoPipeIstream.cxx',
  'src/istream/DelayedIstream.cxx',
  'src/istream/istream_hold.cxx',
  'src/istream/OptionalIstream.cxx',
  'src/istream/istream_deflate.cxx',
  'src/istream/SubstIstream.cxx',
  'src/istream/YamlSubstIstream.cxx',
  'src/istream/BufferedIstream.cxx',
  'src/istream/ByteIstream.cxx',
  'src/istream/FourIstream.cxx',
  'src/istream/istream_iconv.cxx',
  'src/istream/istream_trace.cxx',
  'src/istream/FailIstream.cxx',
  'src/istream/InjectIstream.cxx',
  'src/istream/istream_catch.cxx',
  'src/istream/istream_later.cxx',
  'src/istream/HeadIstream.cxx',
  'src/istream/istream_tee.cxx',
  'src/istream/ReplaceIstream.cxx',
  'src/istream/istream_notify.cxx',
  'src/istream/sink_null.cxx',
  'src/istream/sink_buffer.cxx',
  'src/istream/sink_header.cxx',
  'src/istream/StringSink.cxx',
  'src/istream/sink_fd.cxx',
  include_directories: inc,
  dependencies: [
    zlib,
    libyamlcpp,
  ])
istream_dep = declare_dependency(link_with: istream,
                                dependencies: [event_dep, pool_dep, system_dep])

expand = static_library('expand',
  'src/regex.cxx',
  'src/pexpand.cxx',
  include_directories: inc,
  dependencies: [
    libpcre,
  ])
expand_dep = declare_dependency(link_with: expand,
                                dependencies: [putil_dep])

adata = static_library('adata',
  'libcommon/src/adata/ExpandableStringList.cxx',
  include_directories: inc,
  dependencies: [
  ])
adata_dep = declare_dependency(link_with: adata)

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
  include_directories: inc,
)
istream_spawn_dep = declare_dependency(
  link_with: istream_spawn,
  dependencies: [
    spawn_dep,
    istream_dep,
  ],
)

raddress = static_library('raddress',
  'src/regex.cxx',
  'src/cgi_address.cxx',
  'src/file_address.cxx',
  'src/delegate/Address.cxx',
  'src/nfs/Address.cxx',
  'src/lhttp_address.cxx',
  'src/http_address.cxx',
  'src/address_list.cxx',
  'src/ResourceAddress.cxx',
  include_directories: inc,
  dependencies: [
    libpcre,
  ])
raddress_dep = declare_dependency(
  link_with: raddress,
  dependencies: [
    net_dep,
    spawn_dep,
    adata_dep,
    expand_dep,
  ],
)

was_common = static_library('was_common',
  'src/was/Control.cxx',
  'src/was/Output.cxx',
  'src/was/Input.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
  ],
)
was_common_dep = declare_dependency(
  link_with: was_common,
  dependencies: [
    istream_dep,
    event_net_dep,
    event_dep,
    net_dep,
    putil_dep,
    http_dep,
  ],
)

was_client = static_library('was_client',
  'src/was/Client.cxx',
  'src/was/Stock.cxx',
  'src/was/Launch.cxx',
  'src/was/Glue.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
  ])
was_client_dep = declare_dependency(
  link_with: was_client,
  dependencies: [
    spawn_dep,
    raddress_dep,
    was_common_dep,
    stopwatch_dep,
  ],
)

was_server = static_library('was_server',
  'src/was/Server.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
    was_common_dep,
  ])

http_util = static_library('http_util',
  'src/http_string.cxx',
  'src/http_upgrade.cxx',
  'src/http_util.cxx',
  'src/header_parser.cxx',
  'src/header_writer.cxx',
  include_directories: inc,
)
http_util_dep = declare_dependency(link_with: http_util,
                                   dependencies: [pool_dep, http_dep])

cookie = static_library('cookie',
  'src/cookie_jar.cxx',
  'src/cookie_client.cxx',
  'src/cookie_server.cxx',
  'src/cookie_string.cxx',
  include_directories: inc,
)
cookie_dep = declare_dependency(link_with: cookie,
                                dependencies: [shm_dep, http_util_dep, putil_dep])

thread_pool = static_library('thread_queue',
  'src/thread_queue.cxx',
  'src/thread_worker.cxx',
  'src/thread_pool.cxx',
  include_directories: inc,
  dependencies: [
    event_dep,
    eutil_dep,
  ])
thread_pool_dep = declare_dependency(
  link_with: thread_pool,
  dependencies: [
    event_dep,
    eutil_dep,
  ],
)

socket = static_library('socket',
  'src/fs/FilteredSocket.cxx',
  'src/fs/Ptr.cxx',
  'src/fs/NopSocketFilter.cxx',
  'src/fs/NopThreadSocketFilter.cxx',
  'src/fs/ThreadSocketFilter.cxx',
  'src/fs/Lease.cxx',
  'src/fs/Stock.cxx',
  'src/fs/Balancer.cxx',
  include_directories: inc,
)
socket_dep = declare_dependency(link_with: socket,
                               dependencies: [event_net_dep])

memcached_client = static_library('memcached_client',
  'src/memcached/memcached_client.cxx',
  'src/memcached/memcached_stock.cxx',
  'src/memcached/memcached_packet.cxx',
  include_directories: inc,
)
memcached_client_dep = declare_dependency(
  link_with: memcached_client,
  dependencies: [
    istream_dep,
    socket_dep,
  ],
)

subdir('libcommon/src/translation')

libcommon_translation_dep = translation_dep

translation2 = static_library(
  'translation2',
  'src/translation/Cache.cxx',
  'src/translation/Stock.cxx',
  'src/translation/Marshal.cxx',
  'src/translation/Client.cxx',
  'src/translation/Transformation.cxx',
  'src/translation/Vary.cxx',
  'src/translation/InvalidateParser.cxx',
  include_directories: inc,
)

translation_dep = declare_dependency(
  link_with: translation2,
  dependencies: [
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
  'src/http_body.cxx',
  include_directories: inc,
)
http_common_dep = declare_dependency(link_with: http_common,
                                    dependencies: [istream_dep, io_dep])

http_client = static_library('http_client',
  'src/http_client.cxx',
  include_directories: inc,
)
http_client_dep = declare_dependency(
  link_with: http_client,
  dependencies: [
    socket_dep,
    putil_dep,
    http_util_dep,
    http_common_dep,
    stopwatch_dep,
  ],
)

http_server = static_library('http_server',
  'src/http_server/Request.cxx',
  'src/http_server/http_server.cxx',
  'src/http_server/http_server_send.cxx',
  'src/http_server/http_server_request.cxx',
  'src/http_server/http_server_read.cxx',
  'src/http_server/http_server_response.cxx',
  include_directories: inc,
)
http_server_dep = declare_dependency(
  link_with: http_server,
  dependencies: [
    http_util_dep,
    http_common_dep,
    putil_dep,
    socket_dep,
  ],
)

http_cache = static_library('http_cache',
  'src/http_cache.cxx',
  'src/http_cache_document.cxx',
  'src/http_cache_age.cxx',
  'src/http_cache_heap.cxx',
  'src/http_cache_info.cxx',
  'src/http_cache_rfc.cxx',
  include_directories: inc,
)
http_cache_dep = declare_dependency(
  link_with: http_cache,
  dependencies: [
    eutil_dep,
    http_util_dep,
    istream_dep,
    raddress_dep,
  ],
)

cgi = static_library('cgi',
  'src/cgi/cgi_glue.cxx',
  'src/cgi/cgi_parser.cxx',
  'src/cgi/cgi_client.cxx',
  'src/cgi/cgi_launch.cxx',
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

fcgi_client = static_library('fcgi_client',
  'src/fcgi/Client.cxx',
  'src/fcgi/Remote.cxx',
  'src/fcgi/Request.cxx',
  'src/fcgi/Serialize.cxx',
  'src/fcgi/Stock.cxx',
  'src/fcgi/istream_fcgi.cxx',
  include_directories: inc,
)
fcgi_client_dep = declare_dependency(
  link_with: fcgi_client,
  dependencies: [
    socket_dep,
    putil_dep,
    istream_dep,
    http_util_dep,
  ],
)

nfs_client = static_library('nfs_client',
  'src/nfs/Stock.cxx',
  'src/nfs/Client.cxx',
  'src/nfs/Error.cxx',
  'src/nfs/Cache.cxx',
  'src/nfs/Glue.cxx',
  include_directories: inc,
  dependencies: [
    libnfs,
  ])
nfs_client_dep = declare_dependency(
  link_with: nfs_client,
  dependencies: [
    event_dep,
  ],
)

delegate_client = static_library('delegate_client',
  'src/delegate/Client.cxx',
  'src/delegate/Glue.cxx',
  'src/delegate/HttpRequest.cxx',
  'src/delegate/Stock.cxx',
  include_directories: inc,
)
delegate_client_dep = declare_dependency(
  link_with: delegate_client,
  dependencies: [
    socket_dep,
    putil_dep,
    stock_dep,
  ],
)

session = static_library('session',
  'src/bp/session/Session.cxx',
  'src/bp/session/Id.cxx',
  'src/bp/session/Manager.cxx',
  'src/bp/session/Write.cxx',
  'src/bp/session/Read.cxx',
  'src/bp/session/Save.cxx',
  'src/bp/session/DHttpAddress.cxx',
  'src/bp/session/DAddressList.cxx',
  'src/bp/ExternalSession.cxx',
  include_directories: inc,
)
session_dep = declare_dependency(link_with: session,
                                 dependencies: [shm_dep, event_dep,
                                                cookie_dep,
                                                raddress_dep])

widget = static_library('widget',
  'src/widget/Widget.cxx',
  'src/widget/Init.cxx',
  'src/widget/Root.cxx',
  'src/widget/Class.cxx',
  'src/widget/Untrusted.cxx',
  'src/widget/View.cxx',
  'src/widget/Ref.cxx',
  'src/widget/Session.cxx',
  'src/widget/Uri.cxx',
  'src/widget/Approval.cxx',
  'src/widget/Error.cxx',
  'src/args.cxx',
  include_directories: inc,
)
widget_dep = declare_dependency(link_with: widget,
                                dependencies: [putil_dep, raddress_dep,
                                              translation_dep])

processor = static_library('processor',
  'src/xml_parser.cxx',
  'src/bp/XmlProcessor.cxx',
  'src/penv.cxx',
  'src/pheaders.cxx',
  'src/css_parser.cxx',
  'src/bp/CssProcessor.cxx',
  'src/bp/CssRewrite.cxx',
  'src/bp/TextProcessor.cxx',
  'src/esession.cxx',
  include_directories: inc,
)
processor_dep = declare_dependency(link_with: processor,
                                   dependencies: [istream_dep, putil_dep])

control_server = static_library('control_server',
  'src/control/Server.cxx',
  'src/control/Handler.cxx',
  'src/control/Local.cxx',
  'src/control/Distribute.cxx',
  include_directories: inc,
)
control_server_dep = declare_dependency(
  link_with: control_server,
  dependencies: [
    event_net_dep,
  ],
)

executable('cm4all-beng-proxy',
  'src/delegate/RequestHandler.cxx',
  'src/capabilities.cxx',
  'src/address_string.cxx',
  'src/balancer.cxx',
  'src/bot.c',
  'src/bulldog.cxx',
  'libcommon/src/spawn/Server.cxx',
  'libcommon/src/spawn/Launch.cxx',
  'libcommon/src/spawn/Client.cxx',
  'libcommon/src/spawn/Glue.cxx',
  'src/child_socket.cxx',
  'src/child_stock.cxx',
  'src/bp/CommandLine.cxx',
  'src/bp/Config.cxx',
  'src/bp/ConfigParser.cxx',
  'src/bp/Listener.cxx',
  'src/bp/Connection.cxx',
  'src/direct.cxx',
  'src/bp/drop.cxx',
  'src/relocate_uri.cxx',
  'src/fcache.cxx',
  'src/nfs/RequestHandler.cxx',
  'src/nfs/Istream.cxx',
  'src/bp/FileHeaders.cxx',
  'src/bp/FileHandler.cxx',
  'src/bp/ProxyHandler.cxx',
  'src/bp/Global.cxx',
  'src/bp/Handler.cxx',
  'src/bp/Auth.cxx',
  'src/load_file.cxx',
  'src/bp/file_not_found.cxx',
  'src/bp/file_enotdir.cxx',
  'src/bp/file_directory_index.cxx',
  'src/bp/ForwardHeaders.cxx',
  'src/hostname.cxx',
  'src/http_request.cxx',
  'src/HttpResponseHandler.cxx',
  'src/bp/Stats.cxx',
  'src/bp/Control.cxx',
  'src/PipeLease.cxx',
  'src/pipe_stock.cxx',
  'src/pipe_filter.cxx',
  'src/bp/ProxyWidget.cxx',
  'src/random.cxx',
  'src/bp/ForwardRequest.cxx',
  'src/bp/Request.cxx',
  'src/bp/RSession.cxx',
  'src/bp/CollectCookies.cxx',
  'src/bp/RError.cxx',
  'src/bp/Response.cxx',
  'src/bp/GenerateResponse.cxx',
  'src/resource_tag.cxx',
  'src/widget/RewriteUri.cxx',
  'src/serialize.cxx',
  'src/file_request.cxx',
  'src/static_headers.cxx',
  'src/suffix_registry.cxx',
  'src/address_suffix_registry.cxx',
  'src/tcp_stock.cxx',
  'src/tcp_balancer.cxx',
  'src/crash.cxx',
  'src/bp/Worker.cxx',
  'src/istream_rubber.cxx',
  'src/sink_rubber.cxx',
  'src/lhttp_stock.cxx',
  'src/lhttp_request.cxx',
  'src/bp/errdoc.cxx',
  'src/RedirectHttps.cxx',
  'src/widget/FromRequest.cxx',
  'src/widget/FromSession.cxx',
  'src/widget/Registry.cxx',
  'src/widget/Resolver.cxx',
  'src/widget/Dump.cxx',
  'src/widget/Request.cxx',
  'src/widget/Inline.cxx',
  'src/istream_escape.cxx',
  'src/istream_html_escape.cxx',
  'src/DirectResourceLoader.cxx',
  'src/CachedResourceLoader.cxx',
  'src/FilterResourceLoader.cxx',
  'src/BufferedResourceLoader.cxx',
  'src/widget/Frame.cxx',
  'src/istream_gb.cxx',
  'src/istream_unlock.cxx',
  'src/ua_classification.cxx',
  'src/access_log/Client.cxx',
  'src/access_log/Launch.cxx',
  'src/access_log/Glue.cxx',
  'src/access_log/ConfigParser.cxx',
  'src/access_log/ChildErrorLog.cxx',
  'src/PInstance.cxx',
  'src/bp/Instance.cxx',
  'src/bp/Main.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libnfs,
    avahi_dep,
    pool_dep,
    net_dep,
    raddress_dep,
    spawn_dep,
    http_server_dep,
    http_client_dep,
    http_cache_dep,
    ssl_dep,
    translation_dep,
    was_client_dep,
    stopwatch_dep,
    cgi_dep,
    fcgi_client_dep,
    session_dep,
    widget_dep,
    processor_dep,
    nfs_client_dep,
    thread_pool_dep,
    memcached_client_dep,
    delegate_client_dep,
    control_server_dep,
  ])

executable('cm4all-beng-lb',
  'src/capabilities.cxx',
  'src/tcp_stock.cxx',
  'src/tcp_balancer.cxx',
  'src/pipe_stock.cxx',
  'src/balancer.cxx',
  'src/address_string.cxx',
  'src/address_list.cxx',
  'src/address_sticky.cxx',
  'src/client_balancer.cxx',
  'src/bulldog.cxx',
  'src/http_string.cxx',
  'src/http_upgrade.cxx',
  'src/http_util.cxx',
  'src/regex.cxx',
  'src/http_address.cxx',
  'src/HttpResponseHandler.cxx',
  'src/istream_gb.cxx',
  'src/direct.cxx',
  'src/access_log/Client.cxx',
  'src/access_log/Launch.cxx',
  'src/access_log/Glue.cxx',
  'src/access_log/ConfigParser.cxx',
  'src/RedirectHttps.cxx',
  'src/PInstance.cxx',
  'src/lb/Listener.cxx',
  'src/lb/HttpConnection.cxx',
  'src/lb/ResolveConnect.cxx',
  'src/lb/LuaHttpRequestHandler.cxx',
  'src/lb/TranslationHttpRequestHandler.cxx',
  'src/lb/TcpConnection.cxx',
  'src/lb/ForwardHttpRequest.cxx',
  'src/lb/LuaHandler.cxx',
  'src/lb/LuaInitHook.cxx',
  'src/lb/LuaGoto.cxx',
  'src/lb/Stats.cxx',
  'src/lb/Control.cxx',
  'src/lb/JvmRoute.cxx',
  'src/lb/Headers.cxx',
  'src/lb/Session.cxx',
  'src/lb/Check.cxx',
  'src/lb/Cookie.cxx',
  'src/lb/CommandLine.cxx',
  'src/lb/Config.cxx',
  'src/lb/ConfigParser.cxx',
  'src/lb_check.cxx',
  'src/lb/Setup.cxx',
  'src/lb/GotoMap.cxx',
  'src/lb/Branch.cxx',
  'src/lb/Cluster.cxx',
  'src/lb/ClusterConfig.cxx',
  'src/lb/TranslationHandler.cxx',
  'src/lb/TranslationCache.cxx',
  'src/lb/GotoConfig.cxx',
  'src/lb/MonitorController.cxx',
  'src/lb/MonitorRef.cxx',
  'src/lb/MonitorStock.cxx',
  'src/lb/MonitorManager.cxx',
  'src/lb/PingMonitor.cxx',
  'src/lb/SynMonitor.cxx',
  'src/lb/ExpectMonitor.cxx',
  'src/lb/Instance.cxx',
  'src/lb/Main.cxx',

  # the following sources are only necessary for the translation client
  'src/widget/View.cxx',

  include_directories: inc,
  dependencies: [
    threads,
    liblua,
    libpcre,
    libsodium,
    avahi_dep,
    odbus_dep,
    pool_dep,
    io_dep,
    socket_dep,
    event_dep,
    net_dep,
    ssl_dep,
    pg_dep,
    http_server_dep,
    http_client_dep,
    putil_dep,
    eutil_dep,
    stock_dep,
    translation_dep,
    spawn_dep,
    expand_dep,
    stopwatch_dep,
    cookie_dep,
    control_server_dep,
    lua_dep,
    thread_pool_dep,
  ])

executable('cm4all-certdb',
  'src/regex.cxx',
  'src/lb/Config.cxx',
  'src/lb/ConfigParser.cxx',
  'src/lb/GotoConfig.cxx',
  'src/lb/ClusterConfig.cxx',
  'src/certdb/Progress.cxx',
  'src/certdb/WrapKey.cxx',
  'src/certdb/CertDatabase.cxx',
  'src/certdb/Migrate.cxx',
  'src/certdb/FromResult.cxx',
  'src/certdb/Wildcard.cxx',
  'src/certdb/GlueHttpClient.cxx',
  'src/certdb/JWS.cxx',
  'src/certdb/AcmeError.cxx',
  'src/certdb/AcmeSni.cxx',
  'src/certdb/AcmeHttp.cxx',
  'src/certdb/AcmeClient.cxx',
  'src/certdb/AcmeKey.cxx',
  'src/certdb/FakeAcmeClient.cxx',
  'src/certdb/Main.cxx',
  'src/access_log/ConfigParser.cxx',
  include_directories: inc,
  dependencies: [
    libjsoncpp,
    libpcre,
    avahi_dep,
    ssl_dep,
    pg_dep,
    curl_dep,
    net_dep,
    http_dep,
    io_dep,
])

executable('cm4all-beng-proxy-log-cat',
  'src/access_log/Server.cxx',
  'src/access_log/Cat.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
    http_dep,
  ])

executable('cm4all-beng-proxy-log-json',
  'src/access_log/Server.cxx',
  'src/access_log/Json.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
    http_dep,
  ])

executable('cm4all-beng-proxy-log-lua',
  'src/access_log/Server.cxx',
  'src/access_log/Lua.cxx',
  'src/access_log/Launch.cxx',
  include_directories: inc,
  dependencies: [
    lua_dep,
    spawn_dep,
    net_dep,
    http_dep,
    liblua,
  ])

executable('cm4all-beng-proxy-log-traffic',
  'src/access_log/Server.cxx',
  'src/access_log/Traffic.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
    http_dep,
  ])

executable('cm4all-beng-proxy-log-split',
  'src/access_log/Server.cxx',
  'src/access_log/Split.cxx',
  include_directories: inc,
  dependencies: [
    system_dep,
    net_dep,
    http_dep,
  ])

executable('cm4all-beng-proxy-log-forward',
  'src/access_log/Forward.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
    http_dep,
  ])

executable('cm4all-beng-proxy-log-exec',
  'src/access_log/Exec.cxx',
  include_directories: inc,
  dependencies: [
    net_dep,
  ])

executable('cm4all-beng-proxy-log-tee',
  'src/access_log/Launch.cxx',
  'src/access_log/Tee.cxx',
  include_directories: inc,
  dependencies: [
    spawn_dep,
    system_dep,
    net_dep,
  ])

executable('delegate-helper',
  'src/delegate/Helper.cxx',
  include_directories: inc,
  dependencies: [
  ])

install_headers(
  'include/beng-proxy/Control.hxx',
  'include/beng-proxy/Headers.hxx',
  subdir: 'beng-proxy',
)

subdir('test')
subdir('libcommon/test/util')
subdir('libcommon/test/http')
subdir('libcommon/test/io')
subdir('libcommon/test/pg')
