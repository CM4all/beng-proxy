project('CM4all beng-proxy', ['c', 'cpp'], version: '14.0.1',
  default_options: [
    'c_std=c99',
    'cpp_std=c++14'
  ],
)

compiler = meson.get_compiler('cpp')

pkg_mod = import('pkgconfig')
pkg_mod.generate(version: meson.project_version(),
                 name: 'libcm4all-beng-proxy',
                 subdirs: ['cm4all/libbeng-proxy-0'],
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
  '-DHAVE_PIPE2',
  '-DHAVE_ACCEPT4',
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

add_global_arguments(common_flags,
  '-Wmissing-prototypes', '-Wstrict-prototypes',
  '-Wbad-function-cast',
  '-Waggregate-return',
  '-Wnested-externs',
  '-pedantic',
  language: 'c')

add_global_arguments(common_flags,
  '-fno-threadsafe-statics',
  '-fmerge-all-constants',
  '-Wno-variadic-macros',
  '-Wno-missing-field-initializers',
  '-Wno-non-virtual-dtor',
  language: 'cpp')

if debug
  debug_flags = ['-DPOISON', '-DTRACE']
  debug_flags += ['-DDEBUG_POOL_REF']
  debug_flags += ['-DVALGRIND']
else
  debug_flags = []
endif

stopwatch = debug
if stopwatch
  debug_flags += ['-DENABLE_STOPWATCH']
endif

add_global_arguments(debug_flags, language: 'c')
add_global_arguments(debug_flags, language: 'cpp')

threads = dependency('threads')
boost = dependency('boost', modules: ['system', 'filesystem'], version: '>= 1.54')
liblua = dependency('luajit')
libcap = dependency('libcap')
libseccomp = dependency('libseccomp')
libevent = dependency('libevent', version: '>= 2.0.19')
libcurl = dependency('libcurl', version: '>= 7.38')
libsystemd = dependency('libsystemd')
libdbus = dependency('dbus-1')
libpcre = dependency('libpcre')
libsodium = dependency('libsodium')
libssl = dependency('openssl')
libpq = dependency('libpq', version: '>= 8.4')
libjsoncpp = dependency('jsoncpp')
libavahi_client = dependency('avahi-client')
libnfs = dependency('libnfs')
zlib = dependency('zlib')

if compiler.has_header('valgrind/memcheck.h')
  add_global_arguments('-DHAVE_VALGRIND_MEMCHECK_H', language: 'cpp')
  add_global_arguments('-DHAVE_VALGRIND_MEMCHECK_H', language: 'c')
endif

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

util = static_library('util',
  'libcommon/src/util/AllocatedString.cxx',
  'libcommon/src/util/Exception.cxx',
  'libcommon/src/util/HexFormat.c',
  'libcommon/src/util/LeakDetector.cxx',
  'libcommon/src/util/PrintException.cxx',
  'libcommon/src/util/StringBuilder.cxx',
  'libcommon/src/util/StringParser.cxx',
  'libcommon/src/util/StringUtil.cxx',
  'libcommon/src/util/StringView.cxx',
  'src/util/StringSet.cxx',
  'libcommon/src/util/djbhash.c',
  'src/uri/uri_base.cxx',
  'src/uri/Dissect.cxx',
  'src/uri/uri_verify.cxx',
  'src/uri/uri_escape.cxx',
  'src/uri/uri_extract.cxx',
  'src/uri/uri_relative.cxx',
  'src/escape_html.cxx',
  'src/escape_css.cxx',
  include_directories: inc,
  dependencies: [
  ])
util_dep = declare_dependency(link_with: util)

time = static_library('time',
  'libcommon/src/time/gmtime.c',
  'libcommon/src/time/Convert.cxx',
  'libcommon/src/time/ISO8601.cxx',
  include_directories: inc,
  dependencies: [
  ])
time_dep = declare_dependency(link_with: time)

io = static_library('io',
  'src/io/Buffered.cxx',
  'libcommon/src/io/FileDescriptor.cxx',
  'libcommon/src/io/WriteFile.cxx',
  'libcommon/src/io/LineParser.cxx',
  'libcommon/src/io/FileLineParser.cxx',
  'libcommon/src/io/ConfigParser.cxx',
  'libcommon/src/io/Logger.cxx',
  'src/io/StringFile.cxx',
  include_directories: inc,
  dependencies: [
    boost,
  ])
io_dep = declare_dependency(link_with: io,
                            dependencies: [util_dep])

system = static_library('system',
  'src/system/SetupProcess.cxx',
  'src/system/fd_util.c',
  'libcommon/src/system/LargeAllocation.cxx',
  'libcommon/src/system/BindMount.cxx',
  'libcommon/src/system/CapabilityState.cxx',
  'libcommon/src/system/ProcessName.cxx',
  'src/system/Isolate.cxx',
  'src/system/urandom.cxx',
  include_directories: inc,
  dependencies: [
    libcap,
  ])
system_dep = declare_dependency(link_with: system,
                                dependencies: [io_dep])

http = static_library('http',
  'libcommon/src/http/Method.c',
  'libcommon/src/http/Status.c',
  'libcommon/src/http/HeaderName.cxx',
  'libcommon/src/http/List.cxx',
  'libcommon/src/http/Date.cxx',
  'libcommon/src/http/Range.cxx',
  include_directories: inc,
)
http_dep = declare_dependency(link_with: http,
                              dependencies: [util_dep, time_dep])

lua = static_library('lua',
  'libcommon/src/lua/Error.cxx',
  'libcommon/src/lua/Panic.cxx',
  'libcommon/src/lua/RunFile.cxx',
  'libcommon/src/lua/State.cxx',
  include_directories: inc,
  dependencies: [
    liblua,
  ]
)
lua_dep = declare_dependency(link_with: lua)

memory = static_library('memory',
  'src/fb_pool.cxx',
  'src/SlicePool.cxx',
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
  'src/RootPool.cxx',
  'src/tpool.cxx',
  'src/pbuffer.cxx',
  'src/pstring.cxx',
  'src/pool.cxx',
  include_directories: inc,
)
pool_dep = declare_dependency(link_with: pool,
                             dependencies: [memory_dep, io_dep])

putil = static_library('putil',
  'src/expansible_buffer.cxx',
  'src/strmap.cxx',
  'src/PStringSet.cxx',
  'src/puri_base.cxx',
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

event = static_library('event',
  'libcommon/src/event/Loop.cxx',
  'libcommon/src/event/ShutdownListener.cxx',
  'libcommon/src/event/CleanupTimer.cxx',
  'libcommon/src/event/DeferEvent.cxx',
  'libcommon/src/event/SignalEvent.cxx',
  include_directories: inc,
  dependencies: [
    libevent,
  ])
event_dep = declare_dependency(link_with: event,
                               dependencies: [util_dep])

# Utility library using libevent
eutil = static_library('eutil',
  'src/notify.cxx',
  'src/cache.cxx',
  include_directories: inc,
)
eutil_dep = declare_dependency(link_with: eutil,
                              dependencies: [event_dep])

net = static_library('net',
  'src/net/FailureManager.cxx',
  'src/net/FailureInfo.cxx',
  'src/net/FailureRef.cxx',
  'libcommon/src/net/SocketAddress.cxx',
  'libcommon/src/net/StaticSocketAddress.cxx',
  'libcommon/src/net/AllocatedSocketAddress.cxx',
  'libcommon/src/net/MaskedSocketAddress.cxx',
  'libcommon/src/net/IPv4Address.cxx',
  'libcommon/src/net/IPv6Address.cxx',
  'libcommon/src/net/ToString.cxx',
  'libcommon/src/net/HostParser.cxx',
  'libcommon/src/net/AddressInfo.cxx',
  'libcommon/src/net/Resolver.cxx',
  'libcommon/src/net/Parser.cxx',
  'libcommon/src/net/RBindSocket.cxx',
  'libcommon/src/net/RConnectSocket.cxx',
  'libcommon/src/net/Interface.cxx',
  'libcommon/src/net/SocketDescriptor.cxx',
  'libcommon/src/net/UniqueSocketDescriptor.cxx',
  'libcommon/src/net/SocketConfig.cxx',
  'libcommon/src/net/Buffered.cxx',
  'libcommon/src/net/log/Parser.cxx',
  'libcommon/src/net/log/OneLine.cxx',
  include_directories: inc,
)
net_dep = declare_dependency(link_with: net,
                             dependencies: [system_dep, io_dep])

event_net = static_library('event_net',
  'libcommon/src/event/net/ConnectSocket.cxx',
  'libcommon/src/event/net/ServerSocket.cxx',
  'libcommon/src/event/net/UdpListener.cxx',
  'libcommon/src/event/net/SocketWrapper.cxx',
  'libcommon/src/event/net/BufferedSocket.cxx',
  'src/net/PConnectSocket.cxx',
  'src/net/Ping.cxx',
  'src/net/UdpDistribute.cxx',
  include_directories: inc,
  dependencies: [
    libevent,
  ])
event_net_dep = declare_dependency(link_with: event_net,
                                  dependencies: [event_dep, net_dep])

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

ssl = static_library('ssl',
  'src/certdb/CertDatabase.cxx',
  'src/certdb/FromResult.cxx',
  'src/certdb/WrapKey.cxx',
  'src/certdb/Wildcard.cxx',
  'libcommon/src/ssl/Buffer.cxx',
  'libcommon/src/ssl/Dummy.cxx',
  'libcommon/src/ssl/Edit.cxx',
  'libcommon/src/ssl/Request.cxx',
  'libcommon/src/ssl/Certificate.cxx',
  'libcommon/src/ssl/Key.cxx',
  'libcommon/src/ssl/Hash.cxx',
  'libcommon/src/ssl/LoadFile.cxx',
  'libcommon/src/ssl/Time.cxx',
  'libcommon/src/ssl/AltName.cxx',
  'libcommon/src/ssl/Name.cxx',
  'libcommon/src/ssl/Error.cxx',
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
  dependencies: [
    libssl,
  ])
ssl_dep = declare_dependency(link_with: ssl,
                            dependencies: [io_dep, event_dep])

avahi = static_library('avahi',
  'src/avahi/Check.cxx',
  'src/avahi/Client.cxx',
  'src/avahi/Poll.cxx',
  'src/avahi/Explorer.cxx',
  include_directories: inc,
  dependencies: [
    libavahi_client,
  ])
avahi_dep = declare_dependency(link_with: avahi,
                               dependencies: [event_dep])

pg = static_library('pg',
  'libcommon/src/pg/Serial.cxx',
  'libcommon/src/pg/Array.cxx',
  'libcommon/src/pg/Interval.cxx',
  'libcommon/src/pg/Timestamp.cxx',
  'libcommon/src/pg/Connection.cxx',
  'libcommon/src/pg/AsyncConnection.cxx',
  'libcommon/src/pg/Result.cxx',
  'libcommon/src/pg/Error.cxx',
  'libcommon/src/pg/Reflection.cxx',
  include_directories: inc,
  dependencies: [
    libpq,
  ])
pg_dep = declare_dependency(link_with: pg)

curl = static_library('curl',
  'libcommon/src/curl/Version.cxx',
  'libcommon/src/curl/Request.cxx',
  'libcommon/src/curl/Global.cxx',
  'libcommon/src/curl/Init.cxx',
  include_directories: inc,
  dependencies: [
    libcurl,
    event_dep,
  ])
curl_dep = declare_dependency(link_with: curl)

odbus = static_library('odbus',
  'libcommon/src/odbus/Connection.cxx',
  'libcommon/src/odbus/Message.cxx',
  'libcommon/src/odbus/Error.cxx',
  'libcommon/src/odbus/ScopeMatch.cxx',
  include_directories: inc,
  dependencies: [
    libdbus,
  ])

istream = static_library('istream',
  'src/istream/UnusedPtr.cxx',
  'src/istream/TimeoutIstream.cxx',
  'src/istream/istream_pause.cxx',
  'src/istream/istream.cxx',
  'src/istream/istream_memory.cxx',
  'src/istream/istream_null.cxx',
  'src/istream/istream_zero.cxx',
  'src/istream/istream_block.cxx',
  'src/istream/istream_string.cxx',
  'src/istream/istream_file.cxx',
  'src/istream/istream_chunked.cxx',
  'src/istream/istream_dechunk.cxx',
  'src/istream/istream_cat.cxx',
  'src/istream/istream_pipe.cxx',
  'src/istream/istream_delayed.cxx',
  'src/istream/istream_hold.cxx',
  'src/istream/istream_optional.cxx',
  'src/istream/istream_deflate.cxx',
  'src/istream/istream_subst.cxx',
  'src/istream/istream_byte.cxx',
  'src/istream/istream_four.cxx',
  'src/istream/istream_iconv.cxx',
  'src/istream/istream_trace.cxx',
  'src/istream/istream_fail.cxx',
  'src/istream/istream_inject.cxx',
  'src/istream/istream_catch.cxx',
  'src/istream/istream_later.cxx',
  'src/istream/istream_head.cxx',
  'src/istream/istream_tee.cxx',
  'src/istream/istream_replace.cxx',
  'src/istream/istream_notify.cxx',
  'src/istream/sink_null.cxx',
  'src/istream/sink_buffer.cxx',
  'src/istream/sink_header.cxx',
  'src/istream/StringSink.cxx',
  'src/istream/sink_fd.cxx',
  include_directories: inc,
  dependencies: [
    zlib,
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

spawn = static_library('spawn',
  'libcommon/src/spawn/SeccompFilter.cxx',
  'libcommon/src/spawn/SyscallFilter.cxx',
  'libcommon/src/spawn/Systemd.cxx',
  'libcommon/src/spawn/Prepared.cxx',
  'libcommon/src/spawn/Registry.cxx',
  'libcommon/src/spawn/Direct.cxx',
  'libcommon/src/spawn/Init.cxx',
  'libcommon/src/spawn/Interface.cxx',
  'libcommon/src/spawn/Local.cxx',
  'src/spawn/IstreamSpawn.cxx',
  'libcommon/src/spawn/UserNamespace.cxx',
  'libcommon/src/spawn/NetworkNamespace.cxx',
  'libcommon/src/spawn/NamespaceOptions.cxx',
  'libcommon/src/spawn/MountList.cxx',
  'libcommon/src/spawn/JailConfig.cxx',
  'libcommon/src/spawn/JailParams.cxx',
  'libcommon/src/spawn/ChildOptions.cxx',
  'libcommon/src/spawn/CgroupOptions.cxx',
  'libcommon/src/spawn/UidGid.cxx',
  'libcommon/src/spawn/ResourceLimits.cxx',
  'libcommon/src/spawn/RefenceOptions.cxx',
  'libcommon/src/spawn/ConfigParser.cxx',
  include_directories: inc,
  dependencies: [
    libdbus,
    libseccomp,
    libsystemd,
    declare_dependency(link_with: odbus),
  ])
spawn_dep = declare_dependency(link_with: spawn,
                               dependencies: [system_dep, adata_dep, io_dep, expand_dep])

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
    net_dep,
    spawn_dep,
    adata_dep,
    expand_dep,
  ])
raddress_dep = declare_dependency(link_with: raddress)

was_common = static_library('was_common',
  'src/was/Control.cxx',
  'src/was/Output.cxx',
  'src/was/Input.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
    istream_dep,
    event_dep,
    net_dep,
    putil_dep,
    http_dep,
  ])

was_client = static_library('was_client',
  'src/was/Client.cxx',
  'src/was/Stock.cxx',
  'src/was/Launch.cxx',
  'src/was/Glue.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
    spawn_dep,
    raddress_dep,
    declare_dependency(link_with: was_common),
    stopwatch_dep,
  ])

was_server = static_library('was_server',
  'src/was/Server.cxx',
  include_directories: inc,
  dependencies: [
    libwas,
    declare_dependency(link_with: was_common),
  ])

http_util = static_library('http_util',
  'src/http_string.cxx',
  'src/http_upgrade.cxx',
  'src/http_util.cxx',
  'src/header_copy.cxx',
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

socket = static_library('socket',
  'src/filtered_socket.cxx',
  'src/nop_socket_filter.cxx',
  'src/thread_socket_filter.cxx',
  include_directories: inc,
)
socket_dep = declare_dependency(link_with: socket,
                               dependencies: [event_net_dep])

memcached_client = static_library('memcached_client',
  'src/memcached/memcached_client.cxx',
  'src/memcached/memcached_stock.cxx',
  'src/memcached/memcached_packet.cxx',
  include_directories: inc,
  dependencies: [
    istream_dep,
    socket_dep,
  ])

ajp_client = static_library('ajp_client',
  'src/ajp/Client.cxx',
  'src/ajp/Headers.cxx',
  'src/ajp/Protocol.cxx',
  'src/ajp/Serialize.cxx',
  'src/ajp/istream_ajp_body.cxx',
  'src/ajp/Glue.cxx',
  include_directories: inc,
  dependencies: [
    istream_dep,
    stopwatch_dep,
    socket_dep,
    putil_dep,
    http_dep,
  ])

translation = static_library('translation',
  'src/translation/Cache.cxx',
  'src/translation/Stock.cxx',
  'libcommon/src/translation/PReader.cxx',
  'libcommon/src/translation/Parser.cxx',
  'src/translation/Marshal.cxx',
  'src/translation/Client.cxx',
  'src/translation/Transformation.cxx',
  'libcommon/src/translation/Response.cxx',
  'src/translation/Vary.cxx',
  'src/translation/InvalidateParser.cxx',
  include_directories: inc,
)
translation_dep = declare_dependency(link_with: translation,
                                     dependencies: [event_dep, putil_dep, eutil_dep,
                                                    raddress_dep,
                                                    spawn_dep, socket_dep, net_dep, stopwatch_dep])

http_common = static_library('http_common',
  'src/http_body.cxx',
  include_directories: inc,
)
http_common_dep = declare_dependency(link_with: http_common,
                                    dependencies: [istream_dep, io_dep])

http_client = static_library('http_client',
  'src/http_client.cxx',
  include_directories: inc,
  dependencies: [
    socket_dep,
    putil_dep,
    http_util_dep,
    http_common_dep,
    stopwatch_dep,
  ])
http_client_dep = declare_dependency(link_with: http_client)

http_server = static_library('http_server',
  'src/http_server/Request.cxx',
  'src/http_server/http_server.cxx',
  'src/http_server/http_server_send.cxx',
  'src/http_server/http_server_request.cxx',
  'src/http_server/http_server_read.cxx',
  'src/http_server/http_server_response.cxx',
  include_directories: inc,
  dependencies: [
    http_util_dep,
    http_common_dep,
    putil_dep,
    socket_dep,
  ])
http_server_dep = declare_dependency(link_with: http_server)

http_cache = static_library('http_cache',
  'src/http_cache.cxx',
  'src/http_cache_choice.cxx',
  'src/http_cache_document.cxx',
  'src/http_cache_age.cxx',
  'src/http_cache_heap.cxx',
  'src/http_cache_info.cxx',
  'src/http_cache_memcached.cxx',
  'src/http_cache_rfc.cxx',
  include_directories: inc,
  dependencies: [
    eutil_dep,
    http_util_dep,
    istream_dep,
    raddress_dep,
  ])
http_cache_dep = declare_dependency(link_with: http_cache)

cgi = static_library('cgi',
  'src/cgi/cgi_glue.cxx',
  'src/cgi/cgi_parser.cxx',
  'src/cgi/cgi_client.cxx',
  'src/cgi/cgi_launch.cxx',
  include_directories: inc,
  dependencies: [
    event_dep,
    stopwatch_dep,
    istream_dep,
    http_util_dep,
  ])

fcgi_client = static_library('fcgi_client',
  'src/fcgi/Client.cxx',
  'src/fcgi/Remote.cxx',
  'src/fcgi/Request.cxx',
  'src/fcgi/Serialize.cxx',
  'src/fcgi/Stock.cxx',
  'src/fcgi/istream_fcgi.cxx',
  include_directories: inc,
  dependencies: [
    socket_dep,
    putil_dep,
    istream_dep,
    http_util_dep,
  ])

nfs_client = static_library('nfs_client',
  'src/nfs/Stock.cxx',
  'src/nfs/Client.cxx',
  'src/nfs/Error.cxx',
  'src/nfs/Cache.cxx',
  'src/nfs/Glue.cxx',
  include_directories: inc,
  dependencies: [
    libnfs,
    event_dep,
  ])

delegate_client = static_library('delegate_client',
  'src/delegate/Client.cxx',
  'src/delegate/Glue.cxx',
  'src/delegate/HttpRequest.cxx',
  'src/delegate/Stock.cxx',
  include_directories: inc,
  dependencies: [
    socket_dep,
    putil_dep,
    stock_dep,
  ])

session = static_library('session',
  'src/session.cxx',
  'src/session_id.cxx',
  'src/session_manager.cxx',
  'src/session_write.cxx',
  'src/session_read.cxx',
  'src/session_save.cxx',
  'src/session_external.cxx',
  'src/dhttp_address.cxx',
  'src/daddress_list.cxx',
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
  'src/processor.cxx',
  'src/penv.cxx',
  'src/pheaders.cxx',
  'src/css_parser.cxx',
  'src/css_processor.cxx',
  'src/css_rewrite.cxx',
  'src/text_processor.cxx',
  'src/esession.cxx',
  include_directories: inc,
  dependencies: [
    istream_dep,
    putil_dep,
  ])
processor_dep = declare_dependency(link_with: processor,
                                   dependencies: [istream_dep, putil_dep])

control_server = static_library('control_server',
  'src/control/Server.cxx',
  'src/control/Handler.cxx',
  'src/control/Local.cxx',
  'src/control/Distribute.cxx',
  include_directories: inc,
  dependencies: [
    event_net_dep,
  ])
control_server_dep = declare_dependency(link_with: control_server)

executable('cm4all-beng-proxy',
  'src/delegate/RequestHandler.cxx',
  'src/capabilities.cxx',
  'src/abort_close.cxx',
  'src/abort_unref.cxx',
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
  'src/bp_config.cxx',
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
  'src/bp_global.cxx',
  'src/bp/Handler.cxx',
  'src/bp/Auth.cxx',
  'src/load_file.cxx',
  'src/bp/file_not_found.cxx',
  'src/bp/file_enotdir.cxx',
  'src/bp/file_directory_index.cxx',
  'src/header_forward.cxx',
  'src/header_copy.cxx',
  'src/hostname.cxx',
  'src/http_request.cxx',
  'src/http_response.cxx',
  'src/bp/Stats.cxx',
  'src/bp/Control.cxx',
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
  'src/bp_cmdline.cxx',
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
  'src/widget/Frame.cxx',
  'src/istream_gb.cxx',
  'src/istream_unlock.cxx',
  'src/ua_classification.cxx',
  'src/access_log/Client.cxx',
  'src/access_log/Launch.cxx',
  'src/access_log/Glue.cxx',
  'src/access_log/ConfigParser.cxx',
  'src/PInstance.cxx',
  'src/bp/Instance.cxx',
  'src/bp_main.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    boost,
    libnfs,
    libsystemd,
    libdbus,
    declare_dependency(link_with: avahi),
    pool_dep,
    net_dep,
    raddress_dep,
    spawn_dep,
    http_server_dep,
    http_client_dep,
    http_cache_dep,
    ssl_dep,
    translation_dep,
    declare_dependency(link_with: was_client),
    declare_dependency(link_with: ajp_client),
    stopwatch_dep,
    declare_dependency(link_with: cgi),
    declare_dependency(link_with: fcgi_client),
    session_dep,
    widget_dep,
    processor_dep,
    declare_dependency(link_with: nfs_client),
    declare_dependency(link_with: thread_pool),
    declare_dependency(link_with: memcached_client),
    declare_dependency(link_with: delegate_client),
    control_server_dep,
  ])

executable('cm4all-beng-lb',
  'src/abort_close.cxx',
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
  'src/http_response.cxx',
  'src/istream_gb.cxx',
  'src/direct.cxx',
  'src/access_log/Client.cxx',
  'src/access_log/Launch.cxx',
  'src/access_log/Glue.cxx',
  'src/access_log/ConfigParser.cxx',
  'src/thread_queue.cxx',
  'src/thread_worker.cxx',
  'src/thread_pool.cxx',
  'src/RedirectHttps.cxx',
  'src/PInstance.cxx',
  'src/lb/Listener.cxx',
  'src/lb/HttpConnection.cxx',
  'src/lb/LuaHttpRequestHandler.cxx',
  'src/lb/TranslationHttpRequestHandler.cxx',
  'src/lb/TcpConnection.cxx',
  'src/lb/ForwardHttpRequest.cxx',
  'src/lb/LuaHandler.cxx',
  'src/lb/LuaInitHook.cxx',
  'src/lb/LuaGoto.cxx',
  'src/lb_cmdline.cxx',
  'src/lb/Stats.cxx',
  'src/lb/Control.cxx',
  'src/lb/JvmRoute.cxx',
  'src/lb/Headers.cxx',
  'src/lb_session.cxx',
  'src/lb/Check.cxx',
  'src/lb/Cookie.cxx',
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
  'src/lb_main.cxx',

  # the following sources are only necessary for the translation client
  'src/widget/View.cxx',

  include_directories: inc,
  dependencies: [
    threads,
    boost,
    libsystemd,
    libdbus,
    libpq,
    liblua,
    libpcre,
    libsodium,
    declare_dependency(link_with: avahi),
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
  ])

executable('cm4all-certdb',
  'src/regex.cxx',
  'src/lb/Config.cxx',
  'src/lb/ConfigParser.cxx',
  'src/lb/GotoConfig.cxx',
  'src/lb/ClusterConfig.cxx',
  'src/certdb/WrapKey.cxx',
  'src/certdb/CertDatabase.cxx',
  'src/certdb/Migrate.cxx',
  'src/certdb/FromResult.cxx',
  'src/certdb/Wildcard.cxx',
  'src/certdb/GlueHttpClient.cxx',
  'src/certdb/AcmeError.cxx',
  'src/certdb/AcmeClient.cxx',
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
    libjsoncpp,
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

subdir('test')
subdir('libcommon/test/util')
subdir('libcommon/test/http')
subdir('libcommon/test/io')
subdir('libcommon/test/pg')
