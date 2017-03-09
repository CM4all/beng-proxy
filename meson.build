project('CM4all beng-proxy', ['c', 'cpp'], version: '12.0.8',
  default_options: [
    'c_std=c99',
    'cpp_std=c++14'
  ],
)

pkg_mod = import('pkgconfig')
pkg_mod.generate(version: meson.project_version(),
                 name: 'libcm4all-beng-proxy',
                 subdirs: ['cm4all/libbeng-proxy-0'],
                 requires: ['libcm4all-inline >= 0.3.5'],
                 description: 'CM4all BENG proxy library')

if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
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
]

add_global_arguments(common_flags,
  '-Wall',
  '-Wextra',
  '-Wmissing-prototypes', '-Wstrict-prototypes',
  '-Wwrite-strings', '-Wcast-qual', '-Wcast-align', '-Wfloat-equal',
  '-Wbad-function-cast',
  '-Wshadow', '-Wpointer-arith', '-Wsign-compare',
  '-Waggregate-return',
  '-Wmissing-declarations', '-Wmissing-noreturn', '-Wmissing-format-attribute',
  '-Wredundant-decls', '-Wno-long-long', '-Wundef',
  '-Wnested-externs',
  '-Wunused',
  '-Wundef',
  '-pedantic',
  language: 'c')

add_global_arguments(common_flags,
  '-fno-threadsafe-statics',
  '-fmerge-all-constants',
  '-Wall',
  '-Wextra',
  '-Wwrite-strings', '-Wcast-qual', '-Wcast-align', '-Wfloat-equal',
  '-Wshadow', '-Wpointer-arith', '-Wsign-compare',
  '-Wmissing-declarations', '-Wmissing-noreturn', '-Wmissing-format-attribute',
  '-Wredundant-decls', '-Wno-long-long', '-Wundef',
  '-Wno-variadic-macros',
  '-Wno-missing-field-initializers',
  '-Wno-non-virtual-dtor',
  language: 'cpp')

if debug
  debug_flags = ['-DPOISON', '-DTRACE']
  debug_flags += ['-DDEBUG_POOL_REF', '-DCACHE_LOG']
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
libcap = dependency('libcap')
libseccomp = dependency('libseccomp')
libevent = dependency('libevent', version: '>= 2.0.19')
libcurl = dependency('libcurl', version: '>= 7.38')
libsystemd = dependency('libsystemd')
libdbus = dependency('dbus-1')
libpcre = dependency('libpcre')
libssl = dependency('openssl')
libpq = dependency('libpq', version: '>= 8.4')
libjsoncpp = dependency('jsoncpp')
libavahi_client = dependency('avahi-client')
libnfs = dependency('libnfs')
zlib = dependency('zlib')
cppunit = dependency('cppunit')
glib = dependency('glib-2.0', version: '>= 2.24')

libinline = dependency('libcm4all-inline', version: '>= 0.3.4')
libsocket = dependency('libcm4all-socket', version: '>= 0.3.5')
libdaemon = dependency('libcm4all-daemon', version: '>= 0.4.1')
libhttp = dependency('libcm4all-http', version: '>= 1.2.5')
libwas = dependency('libcm4all-was-protocol', version: '>= 1.0')

inc = include_directories('src', 'include', 'libcommon/src')

util = static_library('util',
  'libcommon/src/util/AllocatedString.cxx',
  'libcommon/src/util/Exception.cxx',
  'libcommon/src/util/LeakDetector.cxx',
  'libcommon/src/util/PrintException.cxx',
  'libcommon/src/util/StringBuilder.cxx',
  'libcommon/src/util/StringParser.cxx',
  'libcommon/src/util/StringUtil.cxx',
  'libcommon/src/util/StringView.cxx',
  'src/util/StringSet.cxx',
  'src/util/djbhash.c',
  'src/format.c',
  'src/http_date.cxx',
  'src/gmtime.c',
  'src/uri/uri_base.cxx',
  'src/uri/uri_parser.cxx',
  'src/uri/uri_verify.cxx',
  'src/uri/uri_escape.cxx',
  'src/uri/uri_extract.cxx',
  'src/uri/uri_relative.cxx',
  'src/escape_html.cxx',
  'src/escape_css.cxx',
  include_directories: inc,
  dependencies: [
    libinline,
  ])
util_dep = declare_dependency(link_with: util)

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
    libinline,
  ])
memory_dep = declare_dependency(link_with: memory)

shm = static_library('shm',
  'src/shm/dpool.cxx',
  'src/shm/dstring.cxx',
  'src/shm/String.cxx',
  'src/shm/shm.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libinline, libdaemon,
  ])

pool = static_library('pool',
  'src/AllocatorPtr.cxx',
  'src/RootPool.cxx',
  'src/tpool.cxx',
  'src/pbuffer.cxx',
  'src/pstring.cxx',
  'src/pool.cxx',
  include_directories: inc,
  dependencies: [
    libinline, libdaemon,
    memory_dep,
  ])
pool_dep = declare_dependency(link_with: pool)

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
    libinline,
  ])
putil_dep = declare_dependency(link_with: putil,
  dependencies: [util_dep, pool_dep]
)

event = static_library('event',
  'libcommon/src/event/Loop.cxx',
  'libcommon/src/event/ShutdownListener.cxx',
  'libcommon/src/event/CleanupTimer.cxx',
  'libcommon/src/event/DeferEvent.cxx',
  'libcommon/src/event/SignalEvent.cxx',
  include_directories: inc,
  dependencies: [
    libinline,
    libevent,
    util_dep,
  ])

# Utility library using libevent
eutil = static_library('eutil',
  'src/notify.cxx',
  'src/cache.cxx',
  include_directories: inc,
  dependencies: [
    libinline,
    declare_dependency(link_with: event),
  ])

if stopwatch
  stopwatch = static_library('stopwatch',
    'src/istream_stopwatch.cxx',
    'src/stopwatch.cxx',
    include_directories: inc,
    dependencies: [
      libinline, libdaemon, libsocket,
      pool_dep,
      util_dep,
    ])
  stopwatch_dep = declare_dependency(link_with: stopwatch)
else
  stopwatch_dep = []
endif

stock = static_library('stock',
  'src/stock/Item.cxx',
  'src/stock/Stock.cxx',
  'src/stock/MapStock.cxx',
  'src/stock/MultiStock.cxx',
  'src/stock/Lease.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libdaemon,
    declare_dependency(link_with: event),
    pool_dep,
  ])
stock_dep = declare_dependency(link_with: stock)

system = static_library('system',
  'src/system/StringFile.cxx',
  'src/system/SetupProcess.cxx',
  'src/system/fd-util.c',
  'src/system/fd_util.c',
  'src/system/bind_mount.c',
  'src/system/urandom.cxx',
  include_directories: inc,
  dependencies: [
    libinline,
    util_dep,
  ])
system_dep = declare_dependency(link_with: system)

io = static_library('io',
  'src/io/Buffered.cxx',
  'src/io/FileDescriptor.cxx',
  'src/io/LineParser.cxx',
  'src/io/ConfigParser.cxx',
  include_directories: inc,
  dependencies: [
    boost,
    libinline,
    util_dep,
    system_dep,
  ])

net = static_library('net',
  'src/net/ConnectSocket.cxx',
  'src/net/ServerSocket.cxx',
  'src/net/UdpDistribute.cxx',
  'src/net/UdpListener.cxx',
  'libcommon/src/net/SocketAddress.cxx',
  'libcommon/src/net/StaticSocketAddress.cxx',
  'libcommon/src/net/AllocatedSocketAddress.cxx',
  'libcommon/src/net/Resolver.cxx',
  'libcommon/src/net/Parser.cxx',
  'src/net/RConnectSocket.cxx',
  'libcommon/src/net/Interface.cxx',
  'libcommon/src/net/SocketDescriptor.cxx',
  include_directories: inc,
  dependencies: [
    libinline, libdaemon, libsocket,
    declare_dependency(link_with: event),
    system_dep,
    declare_dependency(link_with: io),
  ])

ssl = static_library('ssl',
  'src/certdb/CertDatabase.cxx',
  'src/certdb/WrapKey.cxx',
  'src/certdb/Wildcard.cxx',
  'src/ssl/Buffer.cxx',
  'src/ssl/Dummy.cxx',
  'src/ssl/Edit.cxx',
  'src/ssl/Certificate.cxx',
  'src/ssl/Key.cxx',
  'src/ssl/Hash.cxx',
  'src/ssl/LoadFile.cxx',
  'src/ssl/Time.cxx',
  'src/ssl/AltName.cxx',
  'src/ssl/Name.cxx',
  'src/ssl/Error.cxx',
  'src/ssl/Basic.cxx',
  'src/ssl/Cache.cxx',
  'src/ssl/NameCache.cxx',
  'src/ssl/ssl_client.cxx',
  'src/ssl/ssl_factory.cxx',
  'src/ssl/DbSniCallback.cxx',
  'src/ssl/FifoBufferBio.cxx',
  'src/ssl/ssl_filter.cxx',
  'src/ssl/ssl_init.cxx',
  include_directories: inc,
  dependencies: [
    libssl,
    libinline, libdaemon,
    util_dep,
    declare_dependency(link_with: event),
  ])

avahi = static_library('avahi',
  'src/avahi/Client.cxx',
  'src/avahi/Poll.cxx',
  include_directories: inc,
  dependencies: [
    libavahi_client,
    libinline, libdaemon,
    util_dep,
    declare_dependency(link_with: event),
  ])

pg = static_library('pg',
  'libcommon/src/pg/Array.cxx',
  'libcommon/src/pg/Connection.cxx',
  'libcommon/src/pg/AsyncConnection.cxx',
  'libcommon/src/pg/Result.cxx',
  include_directories: inc,
  dependencies: [
    libpq,
    libinline,
  ])

curl = static_library('curl',
  'libcommon/src/curl/Version.cxx',
  'libcommon/src/curl/Request.cxx',
  'libcommon/src/curl/Global.cxx',
  'libcommon/src/curl/Init.cxx',
  include_directories: inc,
  dependencies: [
    libcurl,
    libinline,
    util_dep,
    declare_dependency(link_with: event),
  ])

odbus = static_library('odbus',
  'src/odbus/Message.cxx',
  include_directories: inc,
  dependencies: [
    libdbus,
    libinline,
  ])

istream = static_library('istream',
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
  'src/istream/sink_gstring.cxx',
  'src/istream/sink_fd.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    zlib,
    libinline, libdaemon,
    declare_dependency(link_with: event),
    pool_dep,
    system_dep,
    declare_dependency(link_with: io),
  ])

expand = static_library('expand',
  'src/regex.cxx',
  'src/ExpandableStringList.cxx',
  'src/pexpand.cxx',
  include_directories: inc,
  dependencies: [
    libpcre,
    libinline,
    putil_dep,
  ])

spawn = static_library('spawn',
  'src/spawn/SeccompFilter.cxx',
  'src/spawn/Systemd.cxx',
  'src/spawn/Prepared.cxx',
  'src/spawn/Registry.cxx',
  'src/spawn/Direct.cxx',
  'src/spawn/Interface.cxx',
  'src/spawn/Local.cxx',
  'src/spawn/IstreamSpawn.cxx',
  'src/spawn/NamespaceOptions.cxx',
  'src/spawn/mount_list.cxx',
  'src/spawn/JailConfig.cxx',
  'src/spawn/JailParams.cxx',
  'src/spawn/ChildOptions.cxx',
  'src/spawn/CgroupOptions.cxx',
  'src/spawn/UidGid.cxx',
  'src/spawn/ResourceLimits.cxx',
  'src/spawn/RefenceOptions.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libdbus,
    libseccomp,
    libsystemd,
    libinline, libdaemon,
    system_dep,
    declare_dependency(link_with: io),
    declare_dependency(link_with: odbus),
    declare_dependency(link_with: expand),
  ])

raddress = static_library('raddress',
  'src/regex.cxx',
  'src/cgi_address.cxx',
  'src/file_address.cxx',
  'src/delegate/Address.cxx',
  'src/nfs_address.cxx',
  'src/lhttp_address.cxx',
  'src/http_address.cxx',
  'src/address_list.cxx',
  'src/ResourceAddress.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libpcre,
    libinline, libhttp, libsocket,
    declare_dependency(link_with: spawn),
    putil_dep,
    declare_dependency(link_with: expand),
  ])

was_common = static_library('was_common',
  'src/was/was_control.cxx',
  'src/was/was_output.cxx',
  'src/was/was_input.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp, libwas,
    declare_dependency(link_with: istream),
    declare_dependency(link_with: event),
    putil_dep,
    declare_dependency(link_with: io),
  ])

was_client = static_library('was_client',
  'src/was/was_client.cxx',
  'src/was/was_stock.cxx',
  'src/was/was_launch.cxx',
  'src/was/was_glue.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp, libdaemon, libwas,
    declare_dependency(link_with: spawn),
    declare_dependency(link_with: raddress),
    declare_dependency(link_with: was_common),
    stopwatch_dep,
  ])

was_server = static_library('was_server',
  'src/was/was_server.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp, libwas,
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
  dependencies: [
    libinline, libhttp,
    pool_dep,
    util_dep,
  ])

cookie = static_library('cookie',
  'src/cookie_jar.cxx',
  'src/cookie_client.cxx',
  'src/cookie_server.cxx',
  'src/cookie_string.cxx',
  include_directories: inc,
  dependencies: [
    libinline,
    declare_dependency(link_with: shm),
    declare_dependency(link_with: http_util),
    util_dep,
    putil_dep,
  ])

thread_pool = static_library('thread_queue',
  'src/thread_queue.cxx',
  'src/thread_worker.cxx',
  'src/thread_pool.cxx',
  include_directories: inc,
  dependencies: [
    libinline, libdaemon,
    declare_dependency(link_with: event),
    declare_dependency(link_with: eutil),
  ])

socket = static_library('socket',
  'src/socket_wrapper.cxx',
  'src/buffered_socket.cxx',
  'src/filtered_socket.cxx',
  'src/nop_socket_filter.cxx',
  'src/thread_socket_filter.cxx',
  include_directories: inc,
  dependencies: [
    libinline, libsocket, libhttp,
  ])

memcached_client = static_library('memcached_client',
  'src/memcached/memcached_client.cxx',
  'src/memcached/memcached_stock.cxx',
  'src/memcached/memcached_packet.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp,
    declare_dependency(link_with: istream),
    declare_dependency(link_with: socket),
  ])

ajp_client = static_library('ajp_client',
  'src/ajp/ajp_client.cxx',
  'src/ajp/ajp_headers.cxx',
  'src/ajp/ajp_protocol.cxx',
  'src/ajp/ajp_request.cxx',
  'src/ajp/ajp_serialize.cxx',
  'src/ajp/istream_ajp_body.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp,
    declare_dependency(link_with: istream),
    stopwatch_dep,
    declare_dependency(link_with: socket),
    putil_dep,
  ])

translation = static_library('translation',
  'src/translation/Cache.cxx',
  'src/translation/Stock.cxx',
  'src/translation/PReader.cxx',
  'src/translation/Parser.cxx',
  'src/translation/Marshal.cxx',
  'src/translation/Client.cxx',
  'src/translation/Transformation.cxx',
  'src/translation/Response.cxx',
  'src/translation/Vary.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp, libsocket, libdaemon,
    declare_dependency(link_with: event),
    putil_dep,
    declare_dependency(link_with: eutil),
    declare_dependency(link_with: raddress),
    declare_dependency(link_with: spawn),
    declare_dependency(link_with: socket),
    declare_dependency(link_with: net),
    stopwatch_dep,
  ])

http_common = static_library('http_common',
  'src/http_body.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline,
    declare_dependency(link_with: istream),
    declare_dependency(link_with: io),
  ])

http_client = static_library('http_client',
  'src/http_client.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libdaemon, libhttp,
    declare_dependency(link_with: socket),
    putil_dep,
    declare_dependency(link_with: http_util),
    declare_dependency(link_with: http_common),
    stopwatch_dep,
  ])

http_server = static_library('http_server',
  'src/http_server/Request.cxx',
  'src/http_server/http_server.cxx',
  'src/http_server/http_server_send.cxx',
  'src/http_server/http_server_request.cxx',
  'src/http_server/http_server_read.cxx',
  'src/http_server/http_server_response.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp, libdaemon,
    declare_dependency(link_with: event),
    declare_dependency(link_with: istream),
    declare_dependency(link_with: http_util),
    declare_dependency(link_with: http_common),
    putil_dep,
    declare_dependency(link_with: socket),
  ])

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
    glib,
    libinline, libdaemon, libhttp,
    declare_dependency(link_with: eutil),
    declare_dependency(link_with: http_util),
    declare_dependency(link_with: istream),
    declare_dependency(link_with: raddress),
  ])

cgi = static_library('cgi',
  'src/cgi/cgi_glue.cxx',
  'src/cgi/cgi_parser.cxx',
  'src/cgi/cgi_client.cxx',
  'src/cgi/cgi_launch.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libdaemon, libhttp,
    declare_dependency(link_with: event),
    stopwatch_dep,
    declare_dependency(link_with: istream),
    declare_dependency(link_with: http_util),
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
    glib,
    libinline, libdaemon, libhttp,
    declare_dependency(link_with: socket),
    putil_dep,
    declare_dependency(link_with: istream),
    declare_dependency(link_with: http_util),
  ])

nfs_client = static_library('nfs_client',
  'src/nfs_stock.cxx',
  'src/nfs_client.cxx',
  'src/nfs_cache.cxx',
  'src/nfs_request.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libdaemon, libhttp,
    libnfs,
    declare_dependency(link_with: event),
  ])

delegate_client = static_library('delegate_client',
  'src/delegate/Client.cxx',
  'src/delegate/Glue.cxx',
  'src/delegate/HttpRequest.cxx',
  'src/delegate/Stock.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libinline, libhttp, libdaemon,
    declare_dependency(link_with: socket),
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
  dependencies: [
    glib,
    libinline, libhttp, libdaemon,
    declare_dependency(link_with: shm),
    util_dep,
    declare_dependency(link_with: event),
    declare_dependency(link_with: cookie),
    declare_dependency(link_with: raddress),
  ])
session_dep = declare_dependency(link_with: session)

widget = static_library('widget',
  'src/widget.cxx',
  'src/widget_init.cxx',
  'src/widget_root.cxx',
  'src/widget_class.cxx',
  'src/widget_view.cxx',
  'src/widget_ref.cxx',
  'src/widget_session.cxx',
  'src/widget_uri.cxx',
  'src/widget_approval.cxx',
  'src/args.cxx',
  include_directories: inc,
  dependencies: [
    libinline, libhttp,
    putil_dep,
    declare_dependency(link_with: raddress),
    declare_dependency(link_with: translation),
  ])
widget_dep = declare_dependency(link_with: widget)

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
    glib,
    libinline, libhttp, libdaemon,
    declare_dependency(link_with: istream),
    putil_dep,
  ])
processor_dep = declare_dependency(link_with: processor)

control_server = static_library('control_server',
  'src/control_server.cxx',
  'src/control_handler.cxx',
  'src/control_local.cxx',
  'src/control_distribute.cxx',
  include_directories: inc,
  dependencies: [
    libinline,
    declare_dependency(link_with: net),
  ])

executable('cm4all-beng-proxy',
  'src/delegate/RequestHandler.cxx',
  'src/capabilities.cxx',
  'src/abort_close.cxx',
  'src/abort_unref.cxx',
  'src/access_log.cxx',
  'src/address_string.cxx',
  'src/balancer.cxx',
  'src/bot.c',
  'src/bulldog.cxx',
  'src/spawn/Server.cxx',
  'src/spawn/Launch.cxx',
  'src/spawn/Client.cxx',
  'src/spawn/Glue.cxx',
  'src/child_socket.cxx',
  'src/child_stock.cxx',
  'src/bp_config.cxx',
  'src/bp_listener.cxx',
  'src/bp_connection.cxx',
  'src/direct.cxx',
  'src/drop.cxx',
  'src/failure.cxx',
  'src/relocate_uri.cxx',
  'src/fcache.cxx',
  'src/nfs_handler.cxx',
  'src/istream_nfs.cxx',
  'src/file_headers.cxx',
  'src/file_handler.cxx',
  'src/bp_global.cxx',
  'src/handler.cxx',
  'src/auth.cxx',
  'src/load_file.cxx',
  'src/file_not_found.cxx',
  'src/file_enotdir.cxx',
  'src/file_directory_index.cxx',
  'src/header_forward.cxx',
  'src/header_copy.cxx',
  'src/hostname.cxx',
  'src/http_request.cxx',
  'src/http_domain.cxx',
  'src/http_response.cxx',
  'src/bp_stats.cxx',
  'src/bp_control.cxx',
  'src/pipe_stock.cxx',
  'src/pipe_filter.cxx',
  'src/proxy_widget.cxx',
  'src/random.cxx',
  'src/request_forward.cxx',
  'src/request.cxx',
  'src/request_session.cxx',
  'src/collect_cookies.cxx',
  'src/GException.cxx',
  'src/rerror.cxx',
  'src/response.cxx',
  'src/generate_response.cxx',
  'src/resource_tag.cxx',
  'src/rewrite_uri.cxx',
  'src/serialize.cxx',
  'src/file_request.cxx',
  'src/static_headers.cxx',
  'src/suffix_registry.cxx',
  'src/address_suffix_registry.cxx',
  'src/tcp_stock.cxx',
  'src/tcp_balancer.cxx',
  'src/bp_cmdline.cxx',
  'src/crash.cxx',
  'src/bp_worker.cxx',
  'src/istream_rubber.cxx',
  'src/sink_rubber.cxx',
  'src/lhttp_stock.cxx',
  'src/lhttp_request.cxx',
  'src/proxy_handler.cxx',
  'src/errdoc.cxx',
  'src/widget_request.cxx',
  'src/widget_registry.cxx',
  'src/widget_resolver.cxx',
  'src/widget_dump.cxx',
  'src/istream_escape.cxx',
  'src/istream_html_escape.cxx',
  'src/DirectResourceLoader.cxx',
  'src/CachedResourceLoader.cxx',
  'src/FilterResourceLoader.cxx',
  'src/widget_http.cxx',
  'src/inline_widget.cxx',
  'src/frame.cxx',
  'src/istream_gb.cxx',
  'src/istream_unlock.cxx',
  'src/ua_classification.cxx',
  'src/log-client.c',
  'src/log_launch.cxx',
  'src/log_glue.cxx',
  'src/bp_instance.cxx',
  'src/bp_main.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libcap,
    glib,
    boost,
    libnfs,
    libsystemd,
    libdbus,
    libinline, libdaemon, libsocket, libhttp,
    util_dep,
    declare_dependency(link_with: avahi),
    pool_dep,
    declare_dependency(link_with: io),
    declare_dependency(link_with: event),
    declare_dependency(link_with: net),
    declare_dependency(link_with: ssl),
    declare_dependency(link_with: raddress),
    declare_dependency(link_with: spawn),
    declare_dependency(link_with: http_server),
    declare_dependency(link_with: http_client),
    declare_dependency(link_with: http_cache),
    putil_dep,
    declare_dependency(link_with: eutil),
    stock_dep,
    declare_dependency(link_with: shm),
    declare_dependency(link_with: translation),
    declare_dependency(link_with: was_client),
    declare_dependency(link_with: ajp_client),
    declare_dependency(link_with: cookie),
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
    declare_dependency(link_with: control_server),
  ])

executable('cm4all-beng-lb',
  'src/isolate.cxx',
  'src/abort_close.cxx',
  'src/capabilities.cxx',
  'src/tcp_stock.cxx',
  'src/tcp_balancer.cxx',
  'src/pipe_stock.cxx',
  'src/GException.cxx',
  'src/failure.cxx',
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
  'src/istream_gb.cxx',
  'src/direct.cxx',
  'src/access_log.cxx',
  'src/log_glue.cxx',
  'src/log-client.c',
  'src/log_launch.cxx',
  'src/thread_queue.cxx',
  'src/thread_worker.cxx',
  'src/thread_pool.cxx',
  'src/socket_wrapper.cxx',
  'src/buffered_socket.cxx',
  'src/filtered_socket.cxx',
  'src/thread_socket_filter.cxx',
  'src/nop_thread_socket_filter.cxx',
  'src/ping.cxx',
  'src/lb_cmdline.cxx',
  'src/lb_stats.cxx',
  'src/lb_control.cxx',
  'src/lb_listener.cxx',
  'src/lb_connection.cxx',
  'src/lb_log.cxx',
  'src/lb_http.cxx',
  'src/lb_jvm_route.cxx',
  'src/lb_headers.cxx',
  'src/lb_tcp.cxx',
  'src/lb_session.cxx',
  'src/lb_cookie.cxx',
  'src/lb_config.cxx',
  'src/lb_cluster.cxx',
  'src/lb_check.cxx',
  'src/lb_setup.cxx',
  'src/lb_monitor.cxx',
  'src/lb_ping_monitor.cxx',
  'src/lb_syn_monitor.cxx',
  'src/lb_expect_monitor.cxx',
  'src/lb_hmonitor.cxx',
  'src/lb_instance.cxx',
  'src/lb_main.cxx',
  include_directories: inc,
  dependencies: [
    threads,
    libcap,
    glib,
    boost,
    libsystemd,
    libdbus,
    libpq,
    libinline, libdaemon, libsocket, libhttp,
    libpcre,
    util_dep,
    declare_dependency(link_with: avahi),
    pool_dep,
    declare_dependency(link_with: io),
    declare_dependency(link_with: event),
    declare_dependency(link_with: net),
    declare_dependency(link_with: ssl),
    declare_dependency(link_with: pg),
    declare_dependency(link_with: http_server),
    declare_dependency(link_with: http_client),
    putil_dep,
    declare_dependency(link_with: eutil),
    stock_dep,
    declare_dependency(link_with: spawn),
    declare_dependency(link_with: expand),
    stopwatch_dep,
    declare_dependency(link_with: cookie),
    declare_dependency(link_with: control_server),
  ])

executable('cm4all-certdb',
  'src/regex.cxx',
  'src/lb_config.cxx',
  'src/certdb/WrapKey.cxx',
  'src/certdb/CertDatabase.cxx',
  'src/certdb/Wildcard.cxx',
  'src/certdb/GlueHttpClient.cxx',
  'src/certdb/AcmeClient.cxx',
  'src/certdb/Main.cxx',
  include_directories: inc,
  dependencies: [
    glib,
    libjsoncpp,
    libinline, libhttp,
    libpcre,
    util_dep,
    declare_dependency(link_with: ssl),
    declare_dependency(link_with: pg),
    declare_dependency(link_with: curl),
    declare_dependency(link_with: net),
  ])

executable('cm4all-beng-proxy-log-cat',
  'src/log_server.cxx',
  'src/log-cat.c',
  include_directories: inc,
  dependencies: [
    libinline, libhttp,
  ])

executable('cm4all-beng-proxy-log-traffic',
  'src/log_server.cxx',
  'src/log-traffic.c',
  include_directories: inc,
  dependencies: [
    libinline, libhttp,
  ])

executable('cm4all-beng-proxy-log-split',
  'src/log_server.cxx',
  'src/log-split.c',
  include_directories: inc,
  dependencies: [
    libinline, libhttp,
    system_dep,
  ])

executable('cm4all-beng-proxy-log-forward',
  'src/log-forward.c',
  include_directories: inc,
  dependencies: [
    libsocket, libhttp,
    system_dep,
  ])

executable('cm4all-beng-proxy-log-exec',
  'src/log-exec.c',
  include_directories: inc,
  dependencies: [
    libhttp, libsocket,
  ])

executable('cm4all-beng-proxy-log-tee',
  'src/log_launch.cxx',
  'src/log_tee.cxx',
  include_directories: inc,
  dependencies: [
    libinline, libsocket, libdaemon,
    system_dep,
  ])

executable('cm4all-beng-proxy-delegate-helper',
  'src/delegate/Helper.cxx',
  include_directories: inc,
  dependencies: [
    libinline,
  ])

subdir('test')
