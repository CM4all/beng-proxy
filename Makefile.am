ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign 1.11 subdir-objects

EXTRA_DIST = \
	doc/lb.tex \
	doc/beng.tex

#
# Compiler/linker flags
#

LIBEVENT_CFLAGS =
LIBEVENT_LIBS = -levent_core

LIBATTR_CFLAGS =
if HAVE_ATTR_XATTR_H
LIBATTR_LIBS = -lattr
else
LIBATTR_LIBS =
endif

LIBZ_CFLAGS =
LIBZ_LIBS = -lz

AM_CPPFLAGS += \
	-I$(srcdir)/src -I$(srcdir)/include \
	$(LIBCM4ALL_DAEMON_CFLAGS) \
	$(LIBCM4ALL_INLINE_CFLAGS) \
	$(LIBCM4ALL_HTTP_CFLAGS) \
	$(LIBCM4ALL_SOCKET_CFLAGS) \
	$(LIBWAS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(LIBPCRE_CFLAGS) \
	$(BOOST_CPPFLAGS) \
	$(LIBEVENT_CFLAGS) \
	$(LIBATTR_CFLAGS) \
	$(LIBZ_CFLAGS) \
	$(LIBNFS_CFLAGS) \
	-DSPLICE \
	-D_GNU_SOURCE
AM_LDADD = \
	$(GLIB_LIBS) \
	$(LIBEVENT_LIBS)
AM_LDFLAGS += -pthread


#
# Source files
#

if ENABLE_STOPWATCH
STOPWATCH_SRC = \
	src/istream_stopwatch.cxx src/istream_stopwatch.hxx \
	src/stopwatch.cxx src/stopwatch.hxx
else
STOPWATCH_SRC =
endif

#
# Programs
#

bin_PROGRAMS = cm4all-beng-proxy \
	cm4all-beng-lb \
	cm4all-certdb \
	cm4all-beng-proxy-log-cat \
	cm4all-beng-proxy-log-traffic \
	cm4all-beng-proxy-log-split \
	cm4all-beng-proxy-log-forward \
	cm4all-beng-proxy-log-exec \
	cm4all-beng-proxy-log-tee \
	cm4all-beng-proxy-delegate-helper

cm4all_beng_proxy_SOURCES = \
	include/beng-proxy/headers.h \
	include/beng-proxy/translation.h \
	src/AllocatorStats.hxx \
	src/product.h \
	src/FdType.hxx \
	src/capabilities.cxx src/capabilities.hxx \
	src/gerrno.h \
	src/glibfwd.hxx \
	src/abort_close.cxx src/abort_close.hxx \
	src/abort_flag.hxx \
	src/abort_unref.cxx src/abort_unref.hxx \
	src/access_log.cxx src/access_log.hxx \
	src/address_string.cxx src/address_string.hxx \
	src/ajp/ajp_client.cxx src/ajp/ajp_client.hxx \
	src/ajp/ajp_headers.cxx src/ajp/ajp_headers.hxx \
	src/ajp/ajp_protocol.cxx src/ajp/ajp_protocol.hxx \
	src/ajp/ajp_request.cxx src/ajp/ajp_request.hxx \
	src/ajp/ajp_serialize.cxx src/ajp/ajp_serialize.hxx \
	src/ajp/ajp-util.h \
	src/ajp/istream_ajp_body.cxx src/ajp/istream_ajp_body.hxx \
	src/args.cxx src/args.hxx \
	src/background.hxx \
	src/balancer.cxx src/balancer.hxx \
	src/bot.c src/bot.h \
	src/buffered_io.cxx src/buffered_io.hxx \
	src/bulldog.c src/bulldog.h \
	src/cache.cxx src/cache.hxx \
	src/spawn/Protocol.hxx \
	src/spawn/Parser.hxx \
	src/spawn/Builder.hxx \
	src/spawn/Server.cxx src/spawn/Server.hxx \
	src/spawn/Launch.cxx src/spawn/Launch.hxx \
	src/spawn/Client.cxx src/spawn/Client.hxx \
	src/spawn/Glue.cxx src/spawn/Glue.hxx \
	src/cgi/cgi_glue.cxx src/cgi/cgi_glue.hxx \
	src/cgi/cgi_quark.h \
	src/cgi/cgi_parser.cxx src/cgi/cgi_parser.hxx \
	src/cgi/cgi_client.cxx src/cgi/cgi_client.hxx \
	src/cgi/cgi_launch.cxx src/cgi/cgi_launch.hxx \
	src/child_socket.cxx src/child_socket.hxx \
	src/child_stock.cxx src/child_stock.hxx \
	src/bp_config.cxx src/bp_config.hxx \
	src/bp_listener.cxx src/bp_listener.hxx \
	src/bp_connection.cxx src/bp_connection.hxx \
	src/delegate/Handler.hxx \
	src/delegate/Client.cxx src/delegate/Client.hxx \
	src/delegate/Glue.cxx src/delegate/Glue.hxx \
	src/delegate/Protocol.hxx \
	src/delegate/HttpRequest.cxx src/delegate/HttpRequest.hxx \
	src/delegate/Stock.cxx src/delegate/Stock.hxx \
	src/delegate/RequestHandler.cxx \
	src/direct.cxx src/direct.hxx \
	src/drop.cxx src/drop.hxx \
	src/expiry.hxx \
	src/failure.cxx src/failure.hxx \
	src/relocate_uri.cxx src/relocate_uri.hxx \
	src/regex.cxx src/regex.hxx \
	src/expand.cxx src/expand.hxx \
	src/pexpand.cxx src/pexpand.hxx \
	src/fcache.cxx src/fcache.hxx \
	src/fcgi/Client.cxx src/fcgi/Client.hxx \
	src/fcgi/Remote.cxx src/fcgi/Remote.hxx \
	src/fcgi/Protocol.hxx \
	src/fcgi/Request.cxx src/fcgi/Request.hxx \
	src/fcgi/Serialize.cxx src/fcgi/Serialize.hxx \
	src/fcgi/Stock.cxx src/fcgi/Stock.hxx \
	src/fcgi/istream_fcgi.cxx src/fcgi/istream_fcgi.hxx \
	src/was/was_control.cxx src/was/was_control.hxx \
	src/was/was_output.cxx src/was/was_output.hxx \
	src/was/was_input.cxx src/was/was_input.hxx \
	src/was/was_client.cxx src/was/was_client.hxx \
	src/was/was_stock.cxx src/was/was_stock.hxx \
	src/was/was_launch.cxx src/was/was_launch.hxx \
	src/was/was_glue.cxx src/was/was_glue.hxx \
	src/was/Lease.hxx \
	src/file_headers.cxx src/file_headers.hxx \
	src/file_handler.cxx src/file_handler.hxx \
	src/bp_global.cxx src/bp_global.hxx \
	src/handler.cxx src/handler.hxx \
	src/auth.cxx \
	src/load_file.cxx src/load_file.hxx \
	src/file_not_found.cxx src/file_not_found.hxx \
	src/file_enotdir.cxx src/file_enotdir.hxx \
	src/file_directory_index.cxx src/file_directory_index.hxx \
	src/header_forward.cxx src/header_forward.hxx \
	src/header_parser.cxx src/header_parser.hxx \
	src/header_writer.cxx src/header_writer.hxx \
	src/header_copy.cxx src/header_copy.hxx \
	src/hostname.cxx src/hostname.hxx \
	src/http/ChunkParser.hxx \
	src/http_body.cxx src/http_body.hxx \
	src/http_cache.cxx src/http_cache.hxx \
	src/http_cache_choice.cxx src/http_cache_choice.hxx \
	src/http_cache_internal.hxx \
	src/http_cache_document.cxx src/http_cache_document.hxx \
	src/http_cache_age.cxx src/http_cache_age.hxx \
	src/http_cache_heap.cxx src/http_cache_heap.hxx \
	src/http_cache_info.cxx src/http_cache_info.hxx \
	src/http_cache_memcached.cxx src/http_cache_memcached.hxx \
	src/http_cache_rfc.cxx src/http_cache_rfc.hxx \
	src/http_client.cxx src/http_client.hxx \
	src/http_request.cxx src/http_request.hxx \
	src/http_server/Request.hxx \
	src/http_server/Handler.hxx \
	src/http_server/Internal.hxx \
	src/http_server/http_server.cxx src/http_server/http_server.hxx \
	src/http_server/http_server_send.cxx \
	src/http_server/http_server_request.cxx \
	src/http_server/http_server_read.cxx \
	src/http_server/http_server_response.cxx \
	src/http_string.cxx src/http_string.hxx \
	src/http_upgrade.cxx src/http_upgrade.hxx \
	src/http_util.cxx src/http_util.hxx \
	src/http_headers.hxx \
	src/http_quark.h \
	src/http_domain.cxx src/http_domain.hxx \
	src/http_response.cxx src/http_response.hxx \
	src/control_server.cxx src/control_server.hxx \
	src/control_handler.cxx src/control_handler.hxx \
	src/control_local.cxx src/control_local.hxx \
	src/control_distribute.cxx src/control_distribute.hxx \
	src/bp_stats.cxx src/bp_stats.hxx \
	src/bp_control.cxx src/bp_control.hxx \
	src/memcached/memcached_client.cxx src/memcached/memcached_client.hxx \
	src/memcached/memcached_packet.hxx \
	src/memcached/memcached_protocol.hxx \
	src/memcached/memcached_stock.cxx src/memcached/memcached_stock.hxx \
	src/memcached/memcached_packet.cxx \
	src/xml_parser.cxx src/xml_parser.hxx \
	src/pipe_stock.cxx src/pipe_stock.hxx \
	src/pipe_filter.cxx src/pipe_filter.hxx \
	src/PrefixLogger.cxx src/PrefixLogger.hxx \
	src/processor.cxx src/processor.hxx \
	src/penv.cxx src/penv.hxx \
	src/esession.cxx \
	src/proxy_widget.cxx src/proxy_widget.hxx \
	src/random.cxx src/random.hxx \
	src/request_forward.cxx src/request_forward.hxx \
	src/request.cxx src/request.hxx \
	src/request_session.cxx \
	src/rerror.cxx \
	src/response.cxx \
	src/generate_response.cxx src/generate_response.hxx \
	src/resource_tag.cxx src/resource_tag.hxx \
	src/rewrite_uri.cxx src/rewrite_uri.hxx \
	src/serialize.cxx src/serialize.hxx \
	src/socket_wrapper.cxx src/socket_wrapper.hxx \
	src/buffered_socket.cxx src/buffered_socket.hxx \
	src/filtered_socket.cxx src/filtered_socket.hxx \
	src/nop_socket_filter.cxx src/nop_socket_filter.hxx \
	src/file_request.cxx src/file_request.hxx \
	src/static_headers.cxx src/static_headers.hxx \
	src/stock/Item.cxx src/stock/Item.hxx \
	src/stock/GetHandler.hxx \
	src/stock/Class.hxx \
	src/stock/Stats.hxx \
	src/stock/Stock.cxx src/stock/Stock.hxx \
	src/stock/MapStock.cxx src/stock/MapStock.hxx \
	src/stock/MultiStock.cxx src/stock/MultiStock.hxx \
	src/suffix_registry.cxx src/suffix_registry.hxx \
	src/address_suffix_registry.cxx src/address_suffix_registry.hxx \
	src/tcache.cxx src/tcache.hxx \
	src/tcp_stock.cxx src/tcp_stock.hxx \
	src/tcp_balancer.cxx src/tcp_balancer.hxx \
	src/transformation.cxx src/transformation.hxx \
	src/tstock.cxx src/tstock.hxx \
	src/widget.hxx \
	$(STOPWATCH_SRC) \
	src/bp_cmdline.cxx \
	src/completion.h \
	src/crash.cxx src/crash.hxx \
	src/bp_worker.cxx src/bp_worker.hxx \
	src/istream_rubber.cxx src/istream_rubber.hxx \
	src/sink_rubber.cxx src/sink_rubber.hxx \
	src/session.cxx src/session.hxx \
	src/session_id.cxx src/session_id.hxx \
	src/session_manager.cxx src/session_manager.hxx \
	src/session_write.cxx src/session_write.hxx \
	src/session_read.cxx src/session_read.hxx \
	src/session_save.cxx src/session_save.hxx \
	src/cookie_jar.cxx src/cookie_jar.hxx \
	src/cookie_client.cxx src/cookie_client.hxx \
	src/cookie_server.cxx src/cookie_server.hxx \
	src/cookie_string.cxx src/cookie_string.hxx \
	src/translate_quark.hxx \
	src/translate_reader.cxx src/translate_reader.hxx \
	src/translate_parser.cxx src/translate_parser.hxx \
	src/translate_client.cxx src/translate_client.hxx \
	src/TranslateHandler.hxx \
	src/translate_request.hxx \
	src/translate_response.cxx rc/translate_response.hxx \
	src/tvary.cxx src/tvary.hxx \
	src/lhttp_quark.hxx \
	src/lhttp_stock.cxx src/lhttp_stock.hxx \
	src/lhttp_request.cxx src/lhttp_request.hxx \
	src/proxy_handler.cxx \
	src/errdoc.cxx src/errdoc.hxx \
	src/widget.cxx \
	src/widget_init.cxx \
	src/widget_root.cxx \
	src/widget_class.cxx \
	src/widget_view.cxx src/widget_view.hxx \
	src/widget_ref.cxx \
	src/widget_session.cxx \
	src/widget_uri.cxx \
	src/widget_lookup.hxx \
	src/widget-quark.h \
	src/widget_request.cxx src/widget_request.hxx \
	src/widget_registry.cxx src/widget_registry.hxx \
	src/widget_resolver.cxx src/widget_resolver.hxx \
	src/widget_approval.cxx src/widget_approval.hxx \
	src/widget_dump.cxx src/widget_dump.hxx \
	src/escape_pool.cxx src/escape_pool.hxx \
	src/istream_escape.cxx src/istream_escape.hxx \
	src/istream_html_escape.cxx src/istream_html_escape.hxx \
	src/pheaders.cxx src/pheaders.hxx \
	src/css_syntax.hxx \
	src/css_util.hxx \
	src/css_parser.cxx src/css_parser.hxx \
	src/css_processor.cxx src/css_processor.hxx \
	src/css_rewrite.cxx src/css_rewrite.hxx \
	src/text_processor.cxx src/text_processor.hxx \
	src/ResourceLoader.hxx \
	src/DirectResourceLoader.cxx src/DirectResourceLoader.hxx \
	src/CachedResourceLoader.cxx src/CachedResourceLoader.hxx \
	src/FilterResourceLoader.cxx src/FilterResourceLoader.hxx \
	src/widget_http.cxx src/widget_http.hxx \
	src/inline_widget.cxx src/inline_widget.hxx \
	src/frame.cxx src/frame.hxx \
	src/udp_listener.cxx src/udp_listener.hxx \
	src/udp_distribute.cxx src/udp_distribute.hxx \
	src/istream_gb.cxx src/istream_gb.hxx \
	src/address_quark.h \
	src/address_resolver.cxx src/address_resolver.hxx \
	src/istream_unlock.cxx src/istream_unlock.hxx \
	src/ua_classification.cxx src/ua_classification.hxx \
	src/log-client.c src/log-client.h \
	src/log-launch.c src/log-launch.h \
	src/log-glue.c src/log-glue.h \
	src/notify.cxx src/notify.hxx \
	src/thread_job.h \
	src/thread_queue.cxx src/thread_queue.hxx \
	src/thread_worker.cxx src/thread_worker.hxx \
	src/thread_pool.cxx src/thread_pool.hxx \
	src/thread_socket_filter.cxx src/thread_socket_filter.hxx \
	src/bp_instance.cxx src/bp_instance.hxx \
	src/bp_main.cxx
cm4all_beng_proxy_CPPFLAGS = $(AM_CPPFLAGS) \
	$(LIBSYSTEMD_CFLAGS)
cm4all_beng_proxy_LDADD = $(AM_LDADD) \
	-lcap \
	libssl.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libshm.a \
	libnet.a \
	libraddress.a \
	libspawn.a \
	libodbus.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_INLINE_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(DBUS_LIBS) \
	$(LIBSYSTEMD_LIBS) \
	$(LIBPCRE_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBATTR_LIBS) \
	$(LIBNFS_LIBS) \
	$(LIBSSL_LIBS) \
	$(LIBZ_LIBS)

if HAVE_LIBNFS
cm4all_beng_proxy_SOURCES += \
	src/nfs_handler.cxx src/nfs_handler.hxx \
	src/istream_nfs.cxx src/istream_nfs.hxx \
	src/nfs_stock.cxx src/nfs_stock.hxx \
	src/nfs_client.cxx src/nfs_client.hxx \
	src/nfs_cache.cxx src/nfs_cache.hxx \
	src/nfs_request.cxx src/nfs_request.hxx
endif

cm4all_beng_lb_SOURCES = \
	src/isolate.cxx src/isolate.hxx \
	src/abort_close.cxx src/abort_close.hxx \
	src/capabilities.cxx src/capabilities.hxx \
	src/stock/ThreadedStock.hxx \
	src/stock/Item.cxx src/stock/Item.hxx \
	src/stock/Stock.cxx src/stock/Stock.hxx \
	src/stock/MapStock.cxx src/stock/MapStock.hxx \
	src/tcp_stock.cxx \
	src/tcp_balancer.cxx \
	src/pipe_stock.cxx \
	src/failure.cxx \
	src/balancer.cxx \
	src/address_string.cxx src/address_string.hxx \
	src/address_list.cxx \
	src/address_resolver.cxx src/address_resolver.hxx \
	src/address_sticky.cxx src/address_sticky.hxx \
	src/generic_balancer.hxx \
	src/client_balancer.cxx src/client_balancer.hxx \
	src/bulldog.c \
	src/cache.cxx \
	src/http_body.cxx \
	src/http_server/http_server.cxx \
	src/http_server/http_server_send.cxx \
	src/http_server/http_server_request.cxx \
	src/http_server/http_server_read.cxx \
	src/http_server/http_server_response.cxx \
	src/http_client.cxx \
	src/http_string.cxx \
	src/http_upgrade.cxx src/http_upgrade.hxx \
	src/http_util.cxx src/http_util.hxx \
	src/header_writer.cxx \
	src/header_parser.cxx \
	src/regex.cxx src/regex.hxx \
	src/expand.cxx src/expand.hxx \
	src/pexpand.cxx src/pexpand.hxx \
	src/http_address.cxx \
	src/istream_gb.cxx \
	src/buffered_io.cxx \
	$(STOPWATCH_SRC) \
	src/udp_listener.cxx src/udp_listener.hxx \
	src/control_server.cxx src/control_server.hxx \
	src/control_handler.cxx src/control_handler.hxx \
	src/direct.cxx src/direct.hxx \
	src/access_log.cxx src/access_log.hxx \
	src/cookie_server.cxx \
	src/cookie_string.cxx \
	src/log-glue.c \
	src/log-client.c \
	src/log-launch.c \
	src/StickyMode.hxx \
	src/notify.cxx src/notify.hxx \
	src/thread_job.h \
	src/thread_queue.cxx src/thread_queue.hxx \
	src/thread_worker.cxx src/thread_worker.hxx \
	src/thread_pool.cxx src/thread_pool.hxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/thread_socket_filter.cxx src/thread_socket_filter.hxx \
	src/nop_thread_socket_filter.cxx src/nop_thread_socket_filter.hxx \
	src/ping.cxx src/ping.hxx \
	src/LineParser.cxx src/LineParser.hxx \
	src/lb_cmdline.hxx \
	src/lb_cmdline.cxx \
	src/lb_stats.cxx src/lb_stats.hxx \
	src/lb_control.hxx \
	src/lb_control.cxx \
	src/lb_listener.hxx \
	src/lb_listener.cxx \
	src/lb_connection.hxx \
	src/lb_connection.cxx \
	src/lb_log.h src/lb_log.cxx \
	src/lb_http.hxx \
	src/lb_http.cxx \
	src/lb_jvm_route.cxx src/lb_jvm_route.hxx \
	src/lb_headers.cxx src/lb_headers.hxx \
	src/lb_tcp.hxx \
	src/lb_tcp.cxx \
	src/lb_session.hxx \
	src/lb_session.cxx \
	src/lb_cookie.hxx \
	src/lb_cookie.cxx \
	src/lb_config.hxx \
	src/lb_config.cxx \
	src/lb_check.cxx src/lb_check.hxx \
	src/lb_setup.hxx \
	src/lb_setup.cxx \
	src/lb_monitor.hxx \
	src/lb_monitor.cxx \
	src/lb_ping_monitor.hxx \
	src/lb_ping_monitor.cxx \
	src/lb_syn_monitor.hxx \
	src/lb_syn_monitor.cxx \
	src/lb_expect_monitor.hxx \
	src/lb_expect_monitor.cxx \
	src/lb_hmonitor.hxx \
	src/lb_hmonitor.cxx \
	src/lb_instance.cxx src/lb_instance.hxx \
	src/lb_main.cxx
cm4all_beng_lb_CPPFLAGS = $(AM_CPPFLAGS) \
	$(LIBSYSTEMD_CFLAGS) \
	$(LIBPQ_CFLAGS)
cm4all_beng_lb_LDADD = $(AM_LDADD) \
	-lcap \
	libssl.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libnet.a \
	libspawn.a \
	libsystem.a \
	libpg.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_INLINE_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBSYSTEMD_LIBS) \
	$(LIBPCRE_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIBSSL_LIBS) \
	$(LIBPQ_LIBS) \
	$(LIBEVENT_LIBS)

cm4all_certdb_SOURCES = \
	$(STOPWATCH_SRC) \
	src/stock/Item.cxx src/stock/Item.hxx \
	src/stock/Stock.cxx src/stock/Stock.hxx \
	src/stock/MapStock.cxx src/stock/MapStock.hxx \
	src/tcp_stock.cxx src/tcp_stock.hxx \
	src/tcp_balancer.cxx src/tcp_balancer.hxx \
	src/balancer.cxx src/balancer.hxx \
	src/cache.cxx src/cache.hxx \
	src/failure.cxx src/failure.hxx \
	src/bulldog.c \
	src/regex.cxx src/regex.hxx \
	src/expand.cxx src/expand.hxx \
	src/pexpand.cxx src/pexpand.hxx \
	src/address_resolver.cxx src/address_resolver.hxx \
	src/LineParser.cxx \
	src/lb_config.cxx \
	src/http_address.cxx src/http_address.hxx \
	src/http_client.cxx src/http_client.hxx \
	src/http_body.cxx src/http_body.hxx \
	src/http_request.cxx src/http_request.hxx \
	src/http_upgrade.cxx src/http_upgrade.hxx \
	src/http_util.cxx src/http_util.hxx \
	src/header_writer.cxx src/header_writer.hxx \
	src/header_parser.cxx src/header_parser.hxx \
	src/abort_close.cxx src/abort_close.hxx \
	src/buffered_io.cxx src/buffered_io.hxx \
	src/istream_gb.cxx src/istream_gb.hxx \
	src/direct.cxx src/direct.hxx \
	src/socket_wrapper.cxx src/socket_wrapper.hxx \
	src/buffered_socket.cxx src/buffered_socket.hxx \
	src/filtered_socket.cxx src/filtered_socket.hxx \
	src/thread_socket_filter.cxx src/thread_socket_filter.hxx \
	src/thread_queue.cxx src/thread_queue.hxx \
	src/thread_worker.cxx src/thread_worker.hxx \
	src/thread_pool.cxx src/thread_pool.hxx \
	src/notify.cxx src/notify.hxx \
	src/certdb/Config.hxx \
	src/certdb/WrapKey.cxx src/certdb/WrapKey.hxx \
	src/certdb/CertDatabase.cxx src/certdb/CertDatabase.hxx \
	src/certdb/Wildcard.cxx src/certdb/Wildcard.hxx \
	src/certdb/GlueHttpClient.cxx src/certdb/GlueHttpClient.hxx \
	src/certdb/AcmeClient.cxx src/certdb/AcmeClien.hxx \
	src/certdb/Main.cxx
cm4all_certdb_CPPFLAGS = $(AM_CPPFLAGS) \
	$(JSONCPP_CFLAGS) \
	$(LIBSSL_CFLAGS) \
	$(LIBPQ_CFLAGS)
cm4all_certdb_LDADD = $(AM_LDADD) \
	libssl.a \
	libpool.a libmemory.a \
	libnet.a \
	libraddress.a \
	libpg.a \
	libistream.a libpool.a libputil.a libmemory.a libevent.a \
	libnet.a \
	libraddress.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_INLINE_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBPCRE_LIBS) \
	$(JSONCPP_LIBS) \
	$(LIBSSL_LIBS) \
	$(LIBPQ_LIBS)

cm4all_beng_proxy_log_cat_SOURCES = \
	src/log-server.c \
	src/log-cat.c
cm4all_beng_proxy_log_cat_LDADD = \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

cm4all_beng_proxy_log_traffic_SOURCES = \
	src/log-server.c \
	src/log-traffic.c
cm4all_beng_proxy_log_traffic_LDADD = \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

cm4all_beng_proxy_log_split_SOURCES = \
	src/log-server.c \
	src/log-split.c
cm4all_beng_proxy_log_split_LDADD = \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

cm4all_beng_proxy_log_forward_SOURCES = \
	src/log-forward.c
cm4all_beng_proxy_log_forward_LDADD = \
	libsystem.a \
	$(LIBCM4ALL_SOCKET_LIBS)

cm4all_beng_proxy_log_exec_SOURCES = \
	src/log-exec.c
cm4all_beng_proxy_log_exec_LDADD = \
	$(LIBCM4ALL_SOCKET_LIBS)

cm4all_beng_proxy_log_tee_SOURCES = \
	src/log-launch.c \
	src/log_tee.cxx
cm4all_beng_proxy_log_tee_LDADD = \
	libsystem.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

cm4all_beng_proxy_delegate_helper_SOURCES = src/delegate/Helper.cxx

# LLVM

if LLVM

llvmdir = $(libdir)/llvm
llvm_DATA = cm4all-beng-proxy.bc

cm4all_beng_proxy_CSOURCES = $(filter-out %.h,$(cm4all_beng_proxy_SOURCES))
cm4all_beng_proxy_BC = $(cm4all_beng_proxy_CSOURCES:.c=.bc)

$(cm4all_beng_proxy_BC): %.bc: %.c
	$(CLANG) -c -o $@ $< -emit-llvm $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)

cm4all-beng-proxy-unoptimized.bc: $(cm4all_beng_proxy_BC)
	$(LLVM_LINK) -o $@ $^

cm4all-beng-proxy.bc: cm4all-beng-proxy-unoptimized.bc
	$(LLVM_OPT) -o $@ $< -std-compile-opts -std-link-opts -O3

cm4all-beng-proxy.s: cm4all-beng-proxy.bc
	$(LLVM_LLC) -o $@ $< -O3

cm4all-beng-proxy: cm4all-beng-proxy.s
	$(CLANG) -o $@ $^ $(cm4all_beng_proxy_LDFLAGS) $(cm4all_beng_proxy_LDADD)

cm4all_beng_lb_CSOURCES = $(filter-out %.h,$(cm4all_beng_lb_SOURCES))
cm4all_beng_lb_BC = $(cm4all_beng_lb_CSOURCES:.c=.bc)

$(cm4all_beng_lb_BC): %.bc: %.c
	$(CLANG) -c -o $@ $< -emit-llvm $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)

cm4all-beng-lb-unoptimized.bc: $(cm4all_beng_lb_BC)
	$(LLVM_LINK) -o $@ $^

cm4all-beng-lb.bc: cm4all-beng-lb-unoptimized.bc
	$(LLVM_OPT) -o $@ $< -std-compile-opts -std-link-opts -O3

cm4all-beng-lb.s: cm4all-beng-lb.bc
	$(LLVM_LLC) -o $@ $< -O3

cm4all-beng-lb: cm4all-beng-lb.s
	$(CLANG) -o $@ $^ $(cm4all_beng_lb_LDFLAGS) $(cm4all_beng_lb_LDADD)

CLEANFILES = \
	cm4all-beng-lb-unoptimized.bc cm4all-beng-lb.bc cm4all-beng-lb.s cm4all-beng-lb $(cm4all_beng_lb_BC) \
	cm4all-beng-proxy-unoptimized.bc cm4all-beng-proxy.bc cm4all-beng-proxy.s cm4all-beng-proxy $(cm4all_beng_proxy_BC)

endif


#
# Libraries
#

noinst_LIBRARIES = libutil.a libmemory.a libpool.a \
	libsystem.a \
	libodbus.a \
	libspawn.a \
	libshm.a \
	libnet.a \
	libputil.a \
	libraddress.a \
	libevent.a \
	libssl.a \
	libpg.a \
	libistream.a

libutil_a_SOURCES = \
	src/util/BindMethod.hxx \
	src/util/ByteOrder.hxx \
	src/util/Cast.hxx \
	src/util/CharUtil.hxx \
	src/util/ConstBuffer.hxx \
	src/util/DestructObserver.hxx \
	src/util/InstanceList.hxx \
	src/util/IterableSplitString.hxx \
	src/util/StringAPI.hxx \
	src/util/StringUtil.cxx src/util/StringUtil.hxx \
	src/util/StringView.cxx src/util/StringView.hxx \
	src/util/StringCompare.hxx \
	src/util/StringPointer.hxx \
	src/util/TemplateString.hxx \
	src/util/AllocatedString.cxx src/util/AllocatedString.hxx \
	src/util/DeleteDisposer.hxx \
	src/util/Macros.hxx \
	src/util/TrivialArray.hxx \
	src/util/StaticArray.hxx \
	src/util/Domain.hxx \
	src/util/Error.cxx src/util/Error.hxx \
	src/util/PrintException.cxx src/util/PrintException.hxx \
	src/util/djbhash.c src/util/djbhash.h \
	src/util/ScopeExit.hxx \
	src/util/WritableBuffer.hxx \
	src/util/StaticFifoBuffer.hxx \
	src/util/ForeignFifoBuffer.hxx \
	src/util/DynamicFifoBuffer.hxx \
	src/util/RefCount.hxx \
	src/kvlist.hxx \
	src/format.c format.h \
	src/http_date.cxx src/http_date.hxx \
	src/gmtime.c src/gmtime.h \
	src/html_chars.hxx \
	src/async.hxx \
	src/lease.hxx \
	src/trace.h \
	src/uri/uri_base.cxx src/uri/uri_base.hxx \
	src/uri/uri_parser.cxx src/uri/uri_parser.hxx \
	src/uri/uri_verify.cxx src/uri/uri_verify.hxx \
	src/uri/uri_escape.cxx src/uri/uri_escape.hxx \
	src/uri/uri_extract.cxx src/uri/uri_extract.hxx \
	src/uri/uri_relative.cxx src/uri/uri_relative.hxx \
	src/uri/uri_chars.hxx \
	src/uset.h \
	src/crc.h \
	src/escape_class.hxx \
	src/escape_html.cxx src/escape_html.hxx \
	src/escape_css.cxx src/escape_css.hxx

libputil_a_SOURCES = \
	src/expansible_buffer.cxx src/expansible_buffer.hxx \
	src/growing_buffer.cxx src/growing_buffer.hxx \
	src/please.hxx \
	src/strmap.cxx src/strmap.hxx \
	src/strset.cxx src/strset.hxx \
	src/puri_base.cxx src/puri_base.hxx \
	src/puri_edit.cxx src/puri_edit.hxx \
	src/puri_escape.cxx src/puri_escape.hxx \
	src/puri_relative.cxx src/puri_relative.hxx

libmemory_a_SOURCES = \
	src/fb_pool.cxx src/fb_pool.hxx \
	src/SlicePool.cxx src/SlicePool.hxx \
	src/SliceFifoBuffer.cxx src/SliceFifoBuffer.hxx \
	src/rubber.cxx src/rubber.hxx

libpool_a_SOURCES = \
	src/RootPool.cxx src/RootPool.hxx \
	src/tpool.cxx src/tpool.hxx \
	src/pbuffer.cxx src/pbuffer.hxx \
	src/paddress.hxx \
	src/pstring.cxx \
	src/pool.cxx src/pool.hxx

libshm_a_SOURCES = \
	src/shm/dchunk.h \
	src/shm/dpool.cxx src/shm/dpool.hxx \
	src/shm/dstring.cxx \
	src/shm/dbuffer.hxx \
	src/shm/shm.cxx src/shm/shm.hxx

libsystem_a_SOURCES = \
	src/system/Error.hxx \
	src/system/StringFile.cxx src/system/StringFile.hxx \
	src/system/SetupProcess.cxx src/system/SetupProcess.hxx \
	src/system/clock.c src/system/clock.h \
	src/system/fd-util.c src/system/fd-util.h \
	src/system/fd_util.c src/system/fd_util.h \
	src/system/bind_mount.c src/system/bind_mount.h \
	src/system/pivot_root.h \
	src/system/urandom.cxx src/system/urandom.hxx \
	src/system/sigutil.h \
	src/system/mmap.h

libodbus_a_SOURCES = \
	src/odbus/Types.hxx \
	src/odbus/Values.hxx \
	src/odbus/Message.cxx src/odbus/Message.hxx \
	src/odbus/Iter.hxx \
	src/odbus/ReadIter.hxx \
	src/odbus/AppendIter.hxx \
	src/odbus/PendingCall.hxx
libodbus_a_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DBUS_CFLAGS)

libspawn_a_SOURCES = \
	src/spawn/Config.hxx \
	src/spawn/Systemd.cxx src/spawn/Systemd.hxx \
	src/spawn/CgroupState.hxx \
	src/spawn/Prepared.cxx src/spawn/Prepared.hxx \
	src/spawn/ExitListener.hxx \
	src/spawn/Registry.cxx src/spawn/Registry.hxx \
	src/spawn/Direct.cxx src/spawn/Direct.hxx \
	src/spawn/Interface.cxx src/spawn/Interface.hxx \
	src/spawn/Local.cxx src/spawn/Local.hxx \
	src/spawn/IstreamSpawn.cxx src/spawn/IstreamSpawn.hxx \
	src/spawn/NamespaceOptions.cxx src/spawn/NamespaceOptions.hxx \
	src/spawn/mount_list.cxx src/spawn/mount_list.hxx \
	src/spawn/JailConfig.cxx src/spawn/JailConfig.hxx \
	src/spawn/JailParams.cxx src/spawn/JailParams.hxx \
	src/spawn/ChildOptions.cxx src/spawn/ChildOptions.hxx \
	src/spawn/CgroupOptions.cxx src/spawn/CgroupOptions.hxx \
	src/spawn/UidGid.cxx src/spawn/UidGid.hxx \
	src/spawn/ResourceLimits.cxx src/spawn/ResourceLimits.hxx \
	src/spawn/RefenceOptions.cxx src/spawn/RefenceOptions.hxx
libspawn_a_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DBUS_CFLAGS) \
	$(LIBSYSTEMD_CFLAGS)

libnet_a_SOURCES = \
	src/net/Error.cxx src/net/Error.hxx \
	src/net/ConnectSocket.cxx src/net/ConnectSocket.hxx \
	src/net/ServerSocket.cxx src/net/ServerSocket.hxx \
	src/net/SocketAddress.cxx src/net/SocketAddress.hxx \
	src/net/StaticSocketAddress.cxx src/net/StaticSocketAddress.hxx \
	src/net/AllocatedSocketAddress.cxx src/net/AllocatedSocketAddress.hxx \
	src/net/Parser.cxx src/net/Parser.hxx \
	src/net/SocketDescriptor.cxx src/net/SocketDescriptor.hxx

libraddress_a_SOURCES = \
	src/regex.cxx src/regex.hxx \
	src/param_array.hxx src/param_array.cxx \
	src/cgi_address.cxx src/cgi_address.hxx \
	src/file_address.cxx src/file_address.hxx \
	src/delegate/Address.cxx src/delegate/Address.hxx \
	src/nfs_address.cxx src/nfs_address.hxx \
	src/lhttp_address.cxx src/lhttp_address.hxx \
	src/http_address.cxx src/http_address.hxx \
	src/address_list.cxx src/address_list.hxx \
	src/ResourceAddress.cxx src/ResourceAddress.hxx

libevent_a_SOURCES = \
	src/event/Loop.cxx src/event/Loop.hxx \
	src/event/ShutdownListener.cxx src/event/ShutdownListener.hxx \
	src/event/CleanupTimer.cxx src/event/CleanupTimer.hxx \
	src/event/Callback.hxx \
	src/event/Duration.hxx \
	src/event/TimerEvent.hxx \
	src/event/DeferEvent.cxx src/event/DeferEvent.hxx \
	src/event/SignalEvent.hxx
	src/event/Event.hxx

libssl_a_SOURCES = \
	src/certdb/CertDatabase.cxx src/certdb/CertDatabase.hxx \
	src/certdb/WrapKey.cxx src/certdb/WrapKey.hxx \
	src/certdb/Wildcard.cxx src/certdb/Wildcard.hxx \
	src/ssl/Buffer.cxx src/ssl/Buffer.hxx \
	src/ssl/Unique.hxx \
	src/ssl/Key.cxx src/ssl/Key.hxx \
	src/ssl/Hash.cxx src/ssl/Hash.hxx \
	src/ssl/LoadFile.cxx src/ssl/LoadFile.hxx \
	src/ssl/AltName.cxx src/ssl/AltName.hxx \
	src/ssl/Name.cxx src/ssl/Name.hxx \
	src/ssl/MemBio.hxx \
	src/ssl/Base64.hxx \
	src/ssl/Error.cxx src/ssl/Error.hxx \
	src/ssl/Basic.cxx src/ssl/Basic.hxx \
	src/ssl/Cache.cxx src/ssl/Cache.hxx \
	src/ssl/NameCache.cxx src/ssl/NameCache.hxx \
	src/ssl/ssl_quark.hxx \
	src/ssl/ssl_client.cxx src/ssl/ssl_client.hxx \
	src/ssl/ssl_config.hxx \
	src/ssl/ssl_domain.cxx src/ssl/ssl_domain.hxx \
	src/ssl/ssl_factory.cxx src/ssl/ssl_factory.hxx \
	src/ssl/SniCallback.hxx \
	src/ssl/DbSniCallback.cxx src/ssl/DbSniCallback.hxx \
	src/ssl/FifoBufferBio.cxx src/ssl/FifoBufferBio.hxx \
	src/ssl/ssl_filter.cxx src/ssl/ssl_filter.hxx \
	src/ssl/ssl_init.cxx src/ssl/ssl_init.hxx

libpg_a_SOURCES = \
	src/pg/Array.cxx src/pg/Array.hxx \
	src/pg/ParamWrapper.hxx src/pg/DynamicParamWrapper.hxx \
	src/pg/Connection.cxx src/pg/Connection.hxx \
	src/pg/AsyncConnection.cxx src/pg/AsyncConnection.hxx \
	src/pg/Result.cxx src/pg/Result.hxx \
	src/pg/Error.hxx \
	src/pg/CheckError.hxx \
	src/pg/BinaryValue.hxx \
	src/pg/Notify.hxx

libistream_a_SOURCES = \
	src/istream/Handler.hxx \
	src/istream/Bucket.hxx \
	src/istream/Sink.hxx \
	src/istream/FacadeIstream.hxx \
	src/istream/ForwardIstream.hxx \
	src/istream/TimeoutIstream.cxx src/istream/TimeoutIstream.hxx \
	src/istream/istream_pause.cxx src/istream/istream_pause.hxx \
	src/istream/Invoke.hxx \
	src/istream/istream.cxx src/istream/istream.hxx \
	src/istream/Pointer.hxx \
	src/istream/istream_memory.cxx src/istream/istream_memory.hxx \
	src/istream/istream_null.cxx src/istream/istream_null.hxx \
	src/istream/istream_zero.cxx src/istream/istream_zero.hxx \
	src/istream/istream_block.cxx src/istream/istream_block.hxx \
	src/istream/istream_string.cxx src/istream/istream_string.hxx \
	src/istream/istream_file.cxx src/istream/istream_file.hxx \
	src/istream/istream_chunked.cxx \
	src/istream/istream_dechunk.cxx src/istream/istream_dechunk.hxx \
	src/istream/istream_cat.cxx src/istream/istream_cat.hxx \
	src/istream/istream_pipe.cxx src/istream/istream_pipe.hxx \
	src/istream/istream_delayed.cxx src/istream/istream_delayed.hxx \
	src/istream/istream_hold.cxx src/istream/istream_hold.hxx \
	src/istream/istream_optional.cxx src/istream/istream_optional.hxx \
	src/istream/istream_deflate.cxx src/istream/istream_deflate.hxx \
	src/istream/istream_subst.cxx src/istream/istream_subst.hxx \
	src/istream/istream_byte.cxx src/istream/istream_byte.hxx \
	src/istream/istream_four.cxx src/istream/istream_four.hxx \
	src/istream/istream_iconv.cxx src/istream/istream_iconv.hxx \
	src/istream/istream_trace.cxx src/istream/istream_trace.hxx \
	src/istream/istream_fail.cxx src/istream/istream_fail.hxx \
	src/istream/istream_inject.cxx src/istream/istream_inject.hxx \
	src/istream/istream_catch.cxx istream_catch.hxx \
	src/istream/istream_later.cxx src/istream/istream_later.hxx \
	src/istream/istream_head.cxx src/istream/istream_head.hxx \
	src/istream/istream_tee.cxx src/istream/istream_tee.hxx \
	src/istream/istream_replace.cxx src/istream/istream_replace.hxx \
	src/istream/istream_notify.cxx src/istream/istream_notify.hxx \
	src/istream/sink_null.cxx src/istream/sink_null.hxx \
	src/istream/sink_buffer.cxx src/istream/sink_buffer.hxx \
	src/istream/sink_header.cxx \
	src/istream/sink_gstring.cxx src/istream/sink_gstring.hxx \
	src/istream/sink_fd.cxx src/istream/sink_fd.hxx

pkginclude_HEADERS = \
	include/beng-proxy/headers.h \
	include/beng-proxy/translation.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libcm4all-beng-proxy.pc


#
# Test suite
#

if TEST

noinst_PROGRAMS = \
	test/TrafoExample \
	test/run_css_parser \
	test/run_ping \
	test/run_was \
	test/was_mirror \
	test/benchmark-gmtime test/format_http_date \
	test/request_translation \
	test/run_subst \
	test/run_cookie_client \
	test/run_cookie_server \
	test/run_header_parser \
	test/run_html_unescape test/run_html_escape \
	test/run_processor \
	test/run_client_balancer \
	test/run_client \
	test/run_memcached_client \
	test/run_http_cache \
	test/dump_memcached_choice \
	test/cleanup_memcached_choice \
	test/fake_memcached_server \
	test/run_delegate \
	test/dump_udp test/dump_control test/send_control \
	test/run_ua_parser \
	test/run_parser_cdata

if HAVE_LIBNFS
noinst_PROGRAMS += test/run_nfs_client
endif

FILTER_TESTS = test/t_istream_cat test/t_istream_chunked test/t_istream_dechunk \
	test/t_istream_dechunk_verbatim1 \
	test/t_istream_dechunk_verbatim2 \
	test/t_istream_dechunk_verbatim3 \
	test/t_istream_catch \
	test/t_istream_pipe test/t_istream_hold test/t_istream_delayed \
	test/t_istream_subst test/t_istream_deflate test/t_istream_byte \
	test/t_istream_iconv test/t_istream_replace test/t_istream_replace2 \
	test/t_istream_html_escape test/t_istream_fcgi \
	test/t_sink_header \
	test/t_sink_header_empty \
	test/t_istream_processor

check_PROGRAMS = \
	test/t_cache test/t_tcache \
	test/t_regex \
	test/t_header_forward \
	test/t_stock \
	test/t_resource_address \
	test/t_http_address \
	test/t_cgi_address \
	test/t_uri_verify \
	test/t_uri_relative \
	test/t_uri_escape \
	test/t_uri_extract \
	test/t_html_escape \
	test/t_escape_css \
	test/t_expansible_buffer \
	$(FILTER_TESTS) \
	test/t_istream_tee \
	test/t_growing_buffer \
	test/t_balancer \
	test/t_cgi \
	test/run_http_server \
	test/t_http_server \
	test/t_http_client test/t_http_util \
	test/t_http_cache \
	test/t_ajp_client \
	test/t_fcgi_client \
	test/t_was_client \
	test/t_cookie_client \
	test/t_cookie_server \
	test/t_pool \
	test/t_shm test/t_dpool test/t_session \
	test/t_rubber test/t_sink_rubber \
	test/t_slice \
	test/t_relocate_uri \
	test/t_rewrite_uri \
	test/t_widget_registry \
	test/t_widget_resolver \
	test/t_wembed test/t_widget_http \
	test/t_processor \
	test/t_memcached_client

TESTS = test/t_cache test/t_tcache \
	test/t_regex \
	test/t_header_forward \
	test/t_stock \
	test/t_resource_address \
	test/t_http_address \
	test/t_cgi_address \
	test/t_uri_verify \
	test/t_uri_relative \
	test/t_uri_escape \
	test/t_uri_extract \
	test/t_html_escape \
	test/t_escape_css \
	test/t_expansible_buffer \
	$(FILTER_TESTS) \
	test/t_istream_tee \
	test/t_growing_buffer \
	test/t_balancer \
	test/t_cgi \
	test/t-http-server.py test/t_http_server \
	test/t_http_client test/t_http_util \
	test/t_http_cache \
	test/t_ajp_client \
	test/t_fcgi_client \
	test/t_was_client \
	test/t_processor \
	test/t_memcached_client \
	test/t_cookie_client.py test/t_cookie_client \
	test/t_cookie_server \
	test/t_shm test/t_dpool test/t_session \
	test/t_pool \
	test/t_rubber test/t_sink_rubber \
	test/t_slice \
	test/t_relocate_uri \
	test/t_rewrite_uri \
	test/t_widget_registry \
	test/t_widget_resolver \
	test/t_wembed test/t_widget_http

test_TrafoExample_SOURCES = \
	src/trafo/Server.cxx src/trafo/Server.hxx \
	src/trafo/Listener.cxx src/trafo/Listener.hxx \
	src/trafo/Connection.cxx src/trafo/Connection.hxx \
	src/trafo/Request.hxx \
	src/trafo/AllocatedRequest.cxx src/trafo/AllocatedRequest.hxx \
	src/trafo/Response.cxx src/trafo/Response.hxx \
	test/TrafoExample.cxx
test_TrafoExample_LDADD = \
	libevent.a \
	libnet.a \
	libpool.a libmemory.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_INLINE_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBEVENT_LIBS)

test_run_ping_SOURCES = test/run_ping.cxx \
	src/ping.cxx
test_run_ping_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a libevent.a \
	libnet.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

test_run_was_SOURCES = test/run_was.cxx \
	src/was/was_launch.cxx src/was/was_client.cxx \
	src/was/was_control.cxx src/was/was_input.cxx src/was/was_output.cxx \
	src/buffered_io.cxx \
	src/expand.cxx src/pexpand.cxx \
	src/param_array.cxx \
	src/direct.cxx
test_run_was_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libraddress.a \
	libspawn.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBPCRE_LIBS) \
	$(GLIB_LIBS)

test_was_mirror_SOURCES = test/was_mirror.cxx \
	src/was/was_server.cxx src/was/was_server.hxx \
	src/was/was_control.cxx src/was/was_input.cxx src/was/was_output.cxx \
	src/buffered_io.cxx \
	src/direct.cxx
test_was_mirror_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libevent.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

test_t_cache_SOURCES = test/t_cache.cxx \
	src/cache.cxx
test_t_cache_LDADD = $(AM_LDADD) \
	libputil.a \
	libevent.a \
	libpool.a libmemory.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_t_tcache_SOURCES = test/t_tcache.cxx src/tcache.cxx src/cache.cxx \
	test/tconstruct.hxx \
	src/translate_response.cxx \
	src/http_domain.cxx \
	src/widget_view.cxx \
	src/expand.cxx src/pexpand.cxx \
	src/transformation.cxx
test_t_tcache_LDADD = $(AM_LDADD) \
	libraddress.a \
	libevent.a \
	libputil.a \
	libpool.a libmemory.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS) \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) -lrt

test_t_regex_SOURCES = test/t_regex.cxx src/regex.cxx src/expand.cxx src/pexpand.cxx
test_t_regex_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_regex_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(GLIB_LIBS) \
	$(LIBPCRE_LIBS) \
	$(CPPUNIT_LIBS)

test_t_header_forward_SOURCES = test/t_header_forward.cxx \
	src/header_forward.cxx src/header_writer.cxx \
	src/header_copy.cxx \
	src/http_string.cxx \
	src/http_upgrade.cxx src/http_util.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx \
	src/cookie_server.cxx \
	src/cookie_string.cxx \
	src/random.cxx src/random.hxx \
	src/crash.cxx src/crash.hxx \
	src/session_id.cxx
test_t_header_forward_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
	libshm.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS)

test_t_stock_SOURCES = test/t_stock.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx
test_t_stock_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a libevent.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) $(GLIB_LIBS)

test_t_resource_address_SOURCES = test/t_resource_address.cxx \
	src/regex.cxx src/expand.cxx src/pexpand.cxx \
	src/http_address.cxx \
	src/address_list.cxx
test_t_resource_address_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libpool.a libmemory.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS) \
	$(LIBPCRE_LIBS)

test_t_http_address_SOURCES = test/t_http_address.cxx \
	src/http_address.cxx \
	src/regex.cxx src/expand.cxx src/pexpand.cxx \
	src/address_list.cxx
test_t_http_address_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS) \
	$(LIBPCRE_LIBS)

test_t_cgi_address_SOURCES = test/t_cgi_address.cxx \
	src/regex.cxx src/expand.cxx src/pexpand.cxx
test_t_cgi_address_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_cgi_address_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libpool.a libmemory.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS) \
	$(GLIB_LIBS) \
	$(CPPUNIT_LIBS)

test_t_uri_verify_SOURCES = test/t_uri_verify.cxx
test_t_uri_verify_LDADD = $(AM_LDADD) libutil.a

test_t_uri_relative_SOURCES = test/t_uri_relative.cxx
test_t_uri_relative_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_expansible_buffer_SOURCES = test/t_expansible_buffer.cxx
test_t_expansible_buffer_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS)

T_ISTREAM_FILTER_SRC = \
	test/t_istream_filter.hxx \
	src/direct.cxx \
	src/buffered_io.cxx \
	src/pipe_stock.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx

T_ISTREAM_FILTER_LIB = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) $(LIBZ_LIBS)

test_t_istream_cat_SOURCES = test/t_istream_cat.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_cat_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_catch_SOURCES = test/t_istream_catch.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_catch_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_chunked_SOURCES = test/t_istream_chunked.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_chunked_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_dechunk_SOURCES = test/t_istream_dechunk.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_dechunk_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_dechunk_verbatim1_SOURCES = test/t_istream_dechunk_verbatim.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_dechunk_verbatim1_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_dechunk_verbatim2_SOURCES = test/t_istream_dechunk_verbatim.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_dechunk_verbatim2_CPPFLAGS = $(AM_CPPFLAGS) -DT_BYTE
test_t_istream_dechunk_verbatim2_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_dechunk_verbatim3_SOURCES = test/t_istream_dechunk_verbatim.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_dechunk_verbatim3_CPPFLAGS = $(AM_CPPFLAGS) -DT_FOUR
test_t_istream_dechunk_verbatim3_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_pipe_SOURCES = test/t_istream_pipe.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_pipe_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_hold_SOURCES = test/t_istream_hold.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_hold_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_delayed_SOURCES = test/t_istream_delayed.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_delayed_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_subst_SOURCES = test/t_istream_subst.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_subst_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_deflate_SOURCES = test/t_istream_deflate.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_deflate_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_byte_SOURCES = test/t_istream_byte.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_byte_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_iconv_SOURCES = test/t_istream_iconv.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_iconv_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_replace_SOURCES = test/t_istream_replace.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_replace_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_replace2_SOURCES = test/t_istream_replace2.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_replace2_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_html_escape_SOURCES = \
	src/istream_escape.cxx \
	src/istream_html_escape.cxx \
	test/t_istream_html_escape.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_html_escape_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_fcgi_SOURCES = test/t_istream_cat.cxx \
	src/fcgi/istream_fcgi.cxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_istream_fcgi_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_processor_SOURCES = test/t_istream_processor.cxx \
	test/FailingResourceLoader.cxx test/FailingResourceLoader.hxx \
	src/istream_escape.cxx \
	src/istream_html_escape.cxx \
	src/random.cxx src/random.hxx \
	src/crash.cxx src/crash.hxx \
	src/session_id.cxx \
	src/session.cxx src/session_manager.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx \
	src/cookie_string.cxx \
	src/http_string.cxx \
	src/penv.cxx src/processor.cxx \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.cxx src/css_rewrite.cxx \
	src/widget_request.cxx src/widget_approval.cxx \
	src/widget.cxx src/widget_init.cxx src/widget_ref.cxx src/widget_uri.cxx src/args.cxx \
	src/widget_session.cxx src/xml_parser.cxx src/widget_class.cxx \
	src/widget_view.cxx \
	src/rewrite_uri.cxx src/widget_resolver.cxx \
	src/bp_global.cxx src/transformation.cxx \
	src/expand.cxx src/pexpand.cxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_istream_processor_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libshm.a \
	libspawn.a \
	libutil.a \
	$(T_ISTREAM_FILTER_LIB) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS)

test_t_sink_header_SOURCES = test/t_sink_header.cxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_sink_header_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_sink_header_empty_SOURCES = test/t_sink_header_empty.cxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_sink_header_empty_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_tee_SOURCES = test/t_istream_tee.cxx \
	src/istream/sink_close.cxx src/istream/sink_close.hxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_istream_tee_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_growing_buffer_SOURCES = test/t_growing_buffer.cxx \
	src/istream_gb.cxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_growing_buffer_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_balancer_SOURCES = test/t_balancer.cxx \
	test/PoolTest.hxx \
	src/bulldog.c \
	src/balancer.cxx src/cache.cxx src/failure.cxx \
	src/address_list.cxx
test_t_balancer_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_balancer_LDADD = $(AM_LDADD) \
	libevent.a \
	libnet.a \
	libputil.a \
	libpool.a libmemory.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(CPPUNIT_LIBS)

test_t_cgi_SOURCES = test/t_cgi.cxx \
	$(STOPWATCH_SRC) \
	src/cgi/cgi_glue.cxx \
	src/cgi/cgi_client.cxx src/cgi/cgi_launch.cxx \
	src/cgi/cgi_parser.cxx \
	src/regex.cxx \
	src/buffered_io.cxx \
	src/header_parser.cxx \
	src/PrefixLogger.cxx src/PrefixLogger.hxx \
	src/crash.cxx src/crash.hxx \
	src/expand.cxx src/pexpand.cxx \
	src/direct.cxx
test_t_cgi_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libraddress.a \
	libputil.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS)

test_t_http_server_SOURCES = test/t_http_server.cxx \
	src/http_upgrade.cxx src/http_util.cxx \
	src/http_server/http_server.cxx \
	src/http_server/http_server_send.cxx \
	src/http_server/http_server_request.cxx \
	src/http_server/http_server_read.cxx \
	src/http_server/http_server_response.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/address_string.cxx \
	src/buffered_io.cxx \
	src/header_writer.cxx \
	src/istream_gb.cxx \
	src/pipe_stock.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/direct.cxx \
	src/http_body.cxx \
	src/header_parser.cxx
test_t_http_server_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_http_server_SOURCES = test/run_http_server.cxx \
	src/http_upgrade.cxx src/http_util.cxx \
	src/http_server/http_server.cxx \
	src/http_server/http_server_send.cxx \
	src/http_server/http_server_request.cxx \
	src/http_server/http_server_read.cxx \
	src/http_server/http_server_response.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/address_string.cxx \
	src/duplex.cxx src/duplex.hxx \
	src/buffered_io.cxx \
	src/header_writer.cxx \
	src/istream_gb.cxx \
	src/pipe_stock.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/direct.cxx \
	src/http_body.cxx \
	src/header_parser.cxx
test_run_http_server_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_http_client_SOURCES = test/t_http_client.cxx \
	src/t_client.hxx \
	src/http_client.cxx \
	src/http_upgrade.cxx src/http_util.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	$(STOPWATCH_SRC) \
	src/header_writer.cxx \
	src/istream_gb.cxx \
	src/direct.cxx \
	src/http_body.cxx src/header_parser.cxx \
	src/buffered_io.cxx
test_t_http_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_http_util_SOURCES = test/t_http_util.cxx \
	src/http_util.cxx
test_t_http_util_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_http_cache_SOURCES = test/t_http_cache.cxx \
	src/sink_rubber.cxx \
	src/header_parser.cxx \
	src/istream_rubber.cxx \
	src/istream_gb.cxx \
	src/http_cache.cxx src/http_cache_rfc.cxx \
	src/http_cache_info.cxx src/http_cache_document.cxx \
	src/http_cache_heap.cxx src/http_cache_age.cxx \
	src/abort_unref.cxx src/cache.cxx \
	src/header_writer.cxx src/http_util.cxx \
	src/expand.cxx src/pexpand.cxx \
	src/istream_unlock.cxx
test_t_http_cache_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libraddress.a \
	libputil.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBPCRE_LIBS) \
	$(GLIB_LIBS)

test_t_cookie_client_SOURCES = test/t_cookie_client.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx \
	src/cookie_string.cxx \
	src/http_string.cxx \
	src/header_writer.cxx
test_t_cookie_client_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
	libshm.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_cookie_server_SOURCES = test/t_cookie_server.cxx \
	src/cookie_server.cxx \
	src/cookie_string.cxx \
	src/http_string.cxx
test_t_cookie_server_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_shm_SOURCES = test/t_shm.cxx
test_t_shm_LDADD = $(AM_LDADD) \
	libshm.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_dpool_SOURCES = test/t_dpool.cxx
test_t_dpool_LDADD = $(AM_LDADD) \
	libshm.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_session_SOURCES = test/t_session.cxx \
	src/session_id.cxx \
	src/random.cxx src/random.hxx \
	src/crash.cxx src/crash.hxx \
	src/cookie_jar.cxx \
	src/session.cxx src/session_manager.cxx
test_t_session_LDADD = $(AM_LDADD) \
	libshm.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_pool_SOURCES = test/t_pool.cxx
test_t_pool_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_pool_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(CPPUNIT_LIBS)

test_t_rubber_SOURCES = test/t_rubber.cxx
test_t_rubber_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_rubber_LDADD = $(AM_LDADD) \
	libmemory.a \
	$(CPPUNIT_LIBS)

test_t_sink_rubber_SOURCES = \
	src/sink_rubber.cxx \
	test/t_sink_rubber.cxx
test_t_sink_rubber_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_sink_rubber_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(CPPUNIT_LIBS)

test_t_slice_SOURCES = test/t_slice.cxx
test_t_slice_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_slice_LDADD = $(AM_LDADD) \
	libmemory.a \
	$(CPPUNIT_LIBS)

test_t_relocate_uri_SOURCES = test/t_relocate_uri.cxx \
	src/relocate_uri.cxx
test_t_relocate_uri_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_rewrite_uri_SOURCES = test/t_rewrite_uri.cxx \
	test/FailingResourceLoader.cxx test/FailingResourceLoader.hxx \
	src/penv.cxx \
	src/escape_pool.cxx \
	src/istream_escape.cxx \
	src/istream_html_escape.cxx \
	src/rewrite_uri.cxx src/widget_uri.cxx src/widget.cxx \
	src/widget_init.cxx src/widget_view.cxx src/transformation.cxx \
	src/widget_class.cxx \
	src/expand.cxx src/pexpand.cxx \
	src/args.cxx
test_t_rewrite_uri_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBPCRE_LIBS)

test_t_widget_registry_SOURCES = test/t_widget_registry.cxx \
	src/widget_registry.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/transformation.cxx \
	src/tcache.cxx src/cache.cxx \
	src/translate_response.cxx \
	src/widget_view.cxx \
	src/abort_unref.cxx \
	src/expand.cxx src/pexpand.cxx \
	src/http_domain.cxx
test_t_widget_registry_LDADD = $(AM_LDADD) \
	libevent.a \
	libraddress.a \
	libputil.a \
	libpool.a libmemory.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS)

test_t_widget_resolver_SOURCES = test/t_widget_resolver.cxx \
	src/widget_resolver.cxx \
	src/widget_init.cxx
test_t_widget_resolver_LDADD = $(AM_LDADD) \
	libspawn.a \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS) $(GLIB_LIBS)

test_t_wembed_SOURCES = test/t_wembed.cxx \
	src/widget_init.cxx \
	src/widget_approval.cxx \
	src/inline_widget.cxx \
	src/istream_escape.cxx \
	src/istream_html_escape.cxx \
	src/http_util.cxx \
	src/bp_global.cxx
test_t_wembed_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBEVENT_LIBS) \
	$(GLIB_LIBS)

test_t_widget_http_SOURCES = test/t_widget_http.cxx \
	src/widget_http.cxx src/widget_uri.cxx src/widget_request.cxx \
	src/pheaders.cxx \
	src/crash.cxx src/crash.hxx \
	src/esession.cxx \
	src/widget_session.cxx src/session.cxx src/session_manager.cxx src/session_id.cxx \
	src/widget_view.cxx src/widget.cxx src/widget_init.cxx \
	src/widget_class.cxx \
	src/random.cxx src/random.hxx \
	src/args.cxx \
	src/resource_tag.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx \
	src/cookie_server.cxx \
	src/cookie_string.cxx \
	src/header_parser.cxx src/header_forward.cxx src/header_writer.cxx \
	src/header_copy.cxx \
	src/http_string.cxx src/http_util.cxx \
	src/http_upgrade.cxx \
	src/istream_escape.cxx \
	src/transformation.cxx \
	src/expand.cxx src/pexpand.cxx
test_t_widget_http_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libputil.a \
	libshm.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS)

test_benchmark_gmtime_SOURCES = test/benchmark-gmtime.c \
	test/libcore-gmtime.c
test_benchmark_gmtime_LDADD = $(AM_LDADD) libutil.a

test_format_http_date_SOURCES = test/format_http_date.cxx
test_format_http_date_LDADD = $(AM_LDADD) libutil.a

test_request_translation_SOURCES = test/request_translation.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/buffered_io.cxx src/direct.cxx \
	src/translate_reader.cxx src/translate_reader.hxx \
	src/translate_parser.cxx src/translate_parser.hxx \
	src/translate_client.cxx src/tstock.cxx \
	src/translate_response.cxx \
	src/failure.cxx src/bulldog.c src/balancer.cxx src/cache.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/abort_unref.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/expand.cxx src/pexpand.cxx \
	$(STOPWATCH_SRC)
test_request_translation_LDADD = $(AM_LDADD) \
	libraddress.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libnet.a \
	libspawn.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS)

test_run_subst_SOURCES = test/run_subst.cxx \
	test/StdioSink.hxx \
	src/buffered_io.cxx
test_run_subst_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_run_cookie_client_SOURCES = test/run_cookie_client.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx src/http_string.cxx \
	src/cookie_string.cxx \
	src/header_writer.cxx
test_run_cookie_client_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
	libshm.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_cookie_server_SOURCES = test/run_cookie_server.cxx \
	src/cookie_server.cxx src/http_string.cxx \
	src/cookie_string.cxx
test_run_cookie_server_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_header_parser_SOURCES = test/run_header_parser.cxx \
	src/header_parser.cxx
test_run_header_parser_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_html_unescape_SOURCES = \
	src/escape_static.cxx \
	test/run_html_unescape.cxx
test_run_html_unescape_LDADD = $(AM_LDADD) libutil.a

test_run_html_escape_SOURCES = \
	src/escape_static.cxx \
	test/run_html_escape.cxx
test_run_html_escape_LDADD = $(AM_LDADD) libutil.a

test_t_uri_escape_SOURCES = \
	test/t_uri_escape.cxx
test_t_uri_escape_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_uri_escape_LDADD = $(AM_LDADD) libutil.a $(CPPUNIT_LIBS)

test_t_uri_extract_SOURCES = \
	test/t_uri_extract.cxx
test_t_uri_extract_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_uri_extract_LDADD = $(AM_LDADD) libutil.a $(CPPUNIT_LIBS)

test_t_html_escape_SOURCES = \
	src/escape_static.cxx \
	test/t_html_escape.cxx
test_t_html_escape_LDADD = $(AM_LDADD) libutil.a

test_t_escape_css_SOURCES = \
	src/escape_static.cxx \
	test/t_escape_css.cxx
test_t_escape_css_LDADD = $(AM_LDADD) libutil.a

test_run_processor_SOURCES = test/run_processor.cxx \
	test/FailingResourceLoader.cxx test/FailingResourceLoader.hxx \
	src/session_id.cxx src/random.cxx \
	src/processor.cxx src/penv.cxx src/xml_parser.cxx \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.cxx src/css_rewrite.cxx \
	src/widget.cxx src/widget_init.cxx src/widget_approval.cxx src/widget_ref.cxx \
	src/widget_class.cxx \
	src/istream_escape.cxx \
	src/istream_html_escape.cxx \
	src/header_writer.cxx src/args.cxx src/buffered_io.cxx \
	src/header_parser.cxx \
	src/widget_request.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/failure.cxx src/bulldog.c \
	src/balancer.cxx src/http_address.cxx src/cache.cxx \
	src/expand.cxx src/pexpand.cxx \
	$(STOPWATCH_SRC)
test_run_processor_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libnet.a \
	libraddress.a \
	libputil.a \
	libshm.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS)

test_t_processor_SOURCES = test/t_processor.cxx \
	test/FailingResourceLoader.cxx test/FailingResourceLoader.hxx \
	src/session_id.cxx src/random.cxx \
	src/processor.cxx src/penv.cxx src/xml_parser.cxx \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.cxx src/css_rewrite.cxx \
	src/widget.cxx src/widget_init.cxx src/widget_approval.cxx src/widget_ref.cxx \
	src/widget_class.cxx \
	src/istream_escape.cxx \
	src/istream_html_escape.cxx \
	src/header_writer.cxx src/args.cxx src/buffered_io.cxx \
	src/header_parser.cxx \
	src/widget_request.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/failure.cxx src/bulldog.c \
	src/balancer.cxx src/cache.cxx \
	src/expand.cxx src/pexpand.cxx \
	$(STOPWATCH_SRC)
test_t_processor_LDADD = $(AM_LDADD) \
	libnet.a \
	libraddress.a \
	libputil.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libshm.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS) \
	$(LIBEVENT_LIBS)

test_run_client_balancer_SOURCES = test/run_client_balancer.cxx \
	src/address_list.cxx \
	src/client_balancer.cxx \
	src/balancer.cxx \
	src/failure.cxx \
	src/bulldog.c \
	src/cache.cxx \
	$(STOPWATCH_SRC)
test_run_client_balancer_LDADD = $(AM_LDADD) \
	libputil.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libnet.a \
	libsystem.a \
	libutil.a \
	$(LIBEVENT_LIBS) \
	-lrt \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

if HAVE_LIBNFS

test_run_nfs_client_SOURCES = test/run_nfs_client.cxx \
	src/nfs_client.cxx src/istream_nfs.cxx \
	src/direct.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/pipe_stock.cxx \
	src/static_headers.cxx
test_run_nfs_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libevent.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBNFS_LIBS) \
	$(GLIB_LIBS)
endif

test_t_fcgi_client_SOURCES = test/t_fcgi_client.cxx \
	test/fcgi_server.cxx test/fcgi_server.hxx \
	src/fcgi/Client.cxx src/fcgi/Serialize.cxx \
	src/fcgi/istream_fcgi.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/buffered_io.cxx \
	src/header_parser.cxx \
	src/istream_gb.cxx \
	src/direct.cxx
test_t_fcgi_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_was_client_SOURCES = test/t_was_client.cxx \
	src/was/was_server.cxx src/was/was_server.hxx \
	src/was/was_control.cxx src/was/was_input.cxx src/was/was_output.cxx \
	src/was/was_client.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/buffered_io.cxx \
	src/direct.cxx
test_t_was_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_ajp_client_SOURCES = test/t_ajp_client.cxx \
	test/ajp_server.cxx test/ajp_server.hxx \
	src/t_client.hxx src/tio.hxx \
	src/ajp/ajp_client.cxx src/ajp/ajp_serialize.cxx \
	src/ajp/ajp_headers.cxx src/ajp/ajp_protocol.cxx \
	src/ajp/istream_ajp_body.cxx src/ajp/istream_ajp_body.hxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/serialize.cxx \
	src/buffered_io.cxx \
	src/istream_gb.cxx \
	src/direct.cxx
test_t_ajp_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_client_SOURCES = test/run_client.cxx \
	src/thread_socket_filter.cxx \
	src/thread_pool.cxx src/thread_queue.cxx src/thread_worker.cxx \
	src/notify.cxx \
	src/http_client.cxx src/http_body.cxx \
	src/header_writer.cxx src/header_parser.cxx \
	src/http_upgrade.cxx src/http_util.cxx \
	src/ajp/ajp_client.cxx src/ajp/ajp_serialize.cxx \
	src/ajp/ajp_headers.cxx src/ajp/ajp_protocol.cxx \
	src/ajp/istream_ajp_body.cxx src/ajp/istream_ajp_body.hxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/serialize.cxx \
	src/buffered_io.cxx \
	src/istream_gb.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/pipe_stock.cxx \
	$(STOPWATCH_SRC) \
	src/direct.cxx
test_run_client_LDADD = $(AM_LDADD) \
	libssl.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libnet.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(LIBSSL_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_http_cache_SOURCES = test/run_http_cache.cxx \
	src/http_cache_heap.cxx \
	src/http_cache_info.cxx \
	src/http_cache_rfc.cxx \
	src/http_cache_document.cxx \
	src/http_cache_age.cxx \
	src/http_util.cxx \
	src/http_address.cxx \
	src/regex.cxx \
	src/address_list.cxx \
	src/istream_unlock.cxx \
	src/cache.cxx \
	src/expand.cxx src/pexpand.cxx \
	src/istream_rubber.cxx
test_run_http_cache_LDADD = $(AM_LDADD) \
	libraddress.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libspawn.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBPCRE_LIBS)

test_run_memcached_client_SOURCES = test/run_memcached_client.cxx \
	src/memcached/memcached_client.cxx src/memcached/memcached_packet.cxx \
	src/buffered_io.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/pipe_stock.cxx \
	src/direct.cxx
test_run_memcached_client_LDADD = $(LIBEVENT_LIBS) $(GLIB_LIBS) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_dump_memcached_choice_SOURCES = test/dump_memcached_choice.cxx \
	src/memcached/memcached_client.cxx src/memcached/memcached_packet.cxx \
	src/buffered_io.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/direct.cxx \
	src/serialize.cxx
test_dump_memcached_choice_LDADD = $(LIBEVENT_LIBS) $(GLIB_LIBS) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_cleanup_memcached_choice_SOURCES = test/cleanup_memcached_choice.cxx \
	src/address_list.cxx src/address_resolver.cxx \
	src/failure.cxx src/bulldog.c src/balancer.cxx \
	src/tcp_stock.cxx src/tcp_balancer.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/direct.cxx \
	src/cache.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/stock/MapStock.cxx \
	src/memcached/memcached_client.cxx src/memcached/memcached_packet.cxx \
	src/memcached/memcached_stock.cxx \
	src/http_cache_choice.cxx src/http_cache_rfc.cxx \
	src/http_util.cxx \
	src/serialize.cxx \
	src/buffered_io.cxx \
	src/expand.cxx src/pexpand.cxx \
	$(STOPWATCH_SRC)
test_cleanup_memcached_choice_LDADD = $(AM_LDADD) \
	libraddress.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libnet.a \
	libspawn.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBPCRE_LIBS)

test_fake_memcached_server_SOURCES = test/fake_memcached_server.cxx

test_t_memcached_client_SOURCES = test/t_memcached_client.cxx \
	src/memcached/memcached_client.cxx src/memcached/memcached_packet.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/direct.cxx \
	src/buffered_io.cxx 
test_t_memcached_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libputil.a \
	libsystem.a \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_delegate_SOURCES = test/run_delegate.cxx \
	src/delegate/Stock.cxx \
	src/delegate/Client.cxx \
	src/delegate/Glue.cxx \
	src/stock/Item.cxx \
	src/stock/Stock.cxx \
	src/stock/MapStock.cxx \
	src/param_array.cxx \
	src/expand.cxx src/pexpand.cxx
test_run_delegate_LDADD = $(AM_LDADD) \
	libraddress.a \
	libevent.a \
	libpool.a libmemory.a \
	libspawn.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBPCRE_LIBS)

test_dump_udp_SOURCES = test/dump_udp.cxx \
	src/udp_listener.cxx
test_dump_udp_LDADD = $(AM_LDADD) \
	libnet.a libevent.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS)

test_dump_control_SOURCES = test/dump_control.cxx \
	src/control_handler.cxx src/control_handler.hxx \
	src/udp_listener.cxx \
	src/control_server.cxx
test_dump_control_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libnet.a libevent.a \
	libsystem.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS)

test_send_control_SOURCES = test/send_control.cxx
test_send_control_LDADD = $(LIBCM4ALL_SOCKET_LIBS)

test_run_ua_parser_SOURCES = test/run_ua_parser.cxx \
	src/regex.cxx src/expand.cxx \
	src/ua_classification.cxx
test_run_ua_parser_LDADD = $(AM_LDADD) \
	libutil.a \
	$(LIBPCRE_LIBS) \
	$(GLIB_LIBS)

test_run_parser_cdata_SOURCES = test/run_parser_cdata.cxx \
	src/xml_parser.cxx \
	src/buffered_io.cxx
test_run_parser_cdata_LDADD = $(AM_LDADD) \
	libputil.a \
	libistream.a libpool.a libmemory.a libevent.a \
	libsystem.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_run_css_parser_SOURCES = test/run_css_parser.cxx \
	src/css_parser.cxx \
	src/buffered_io.cxx
test_run_css_parser_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a libevent.a \
	libsystem.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) $(GLIB_LIBS)

endif

#
# Sparse
#

SPARSE_FLAGS = -DSPARSE \
	-Wdecl -Wdefault-bitfield-sign -Wdo-while -Wenum-mismatch \
	-Wnon-pointer-null -Wptr-subtraction-blows -Wreturn-void \
	-Wshadow -Wtypesign

SPARSE_FLAGS += $(INCLUDES) $(AM_CPPFLAGS) -DVERSION=0
SPARSE_FLAGS += -D__SCHAR_MAX__=127 -D__SHRT_MAX__=32767 \
	-D__INT_MAX__=2147483647 -D__LONG_MAX__=2147483647

sparse-check:
	sparse $(SPARSE_FLAGS) $(cm4all_beng_proxy_SOURCES)

.PHONY: sparse-check


#
# JavaScript
#

jsdir = $(pkgdatadir)/js
js_DATA = js/beng-proxy.js


#
# Python
#

pythondir = $(pkgdatadir)/python/beng_proxy
python_DATA = python/beng_proxy/__init__.py

pythontdir = $(pkgdatadir)/python/beng_proxy/translation
pythont_DATA = \
	python/beng_proxy/translation/protocol.py \
	python/beng_proxy/translation/request.py \
	python/beng_proxy/translation/response.py \
	python/beng_proxy/translation/serialize.py \
	python/beng_proxy/translation/uri.py \
	python/beng_proxy/translation/__init__.py


#
# Running beng-proxy
#

DEBUG_ARGS = -vvvvvD

debug: cm4all-beng-proxy
	LD_LIBRARY_PATH=/usr/lib/debug:$(LD_LIBRARY_PATH) gdb -x gdbrun --args $< $(DEBUG_ARGS)

VALGRIND_FLAGS = --leak-check=yes --show-reachable=yes
VALGRIND_FLAGS += --track-fds=yes --track-origins=yes
VALGRIND_FLAGS += --suppressions=valgrind.suppressions

valgrind: export G_SLICE=always-malloc
valgrind: cm4all-beng-proxy
	valgrind $(VALGRIND_FLAGS) ./cm4all-beng-proxy $(DEBUG_ARGS)

perf: cm4all-beng-proxy
	perf record -f $< -D -u max -p 8080

# -DNO_DATE_HEADER -DNO_XATTR -DNO_LAST_MODIFIED_HEADER
benchmark: AM_CPPFLAGS += -DNDEBUG -DALWAYS_INLINE -DNO_ACCESS_LOG -DNO_XATTR
benchmark: cm4all-beng-proxy
	./cm4all-beng-proxy -D -u max -p 8080 -w 4

.PHONY: debug valgrind perf benchmark

#
# Documentation
#

if DOCUMENTATION
doc/beng.pdf: $(srcdir)/doc/beng.tex
	@mkdir -p $(@D)
	pdflatex -output-directory $(@D) $<
	pdflatex -output-directory $(@D) $<

doc/lb.pdf: $(srcdir)/doc/lb.tex
	@mkdir -p $(@D)
	pdflatex -output-directory $(@D) $<
	pdflatex -output-directory $(@D) $<
endif

#
# Demo
#

COMAC = /usr/bin/cm4all-comac --quiet-output

.PHONY: upload
upload:
	scp demo/cgi-bin/*.py demo/cgi-bin/*.sh demo/base.html demo/nested_base.html demo/container.html cfatest01:/var/www/vol1/HTO01F/LY/YL/XT/pr_0001/widgets/

COMA_SRC = $(wildcard $(srcdir)/demo/coma/*.cma)
COMA_CLASSES = $(patsubst $(srcdir)/demo/coma/%.cma,demo/coma/%.cls,$(COMA_SRC))

demo-all: $(COMA_CLASSES)

demo-clean:
	rm -f $(COMA_CLASSES)

$(COMA_CLASSES): demo/coma/%.cls: $(srcdir)/demo/coma/%.cma
	@mkdir -p $(@D)
	$(COMAC) -d $(@D) $<
