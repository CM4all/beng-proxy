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
	$(BOOST_CPPFLAGS) \
	$(LIBEVENT_CFLAGS) \
	$(LIBATTR_CFLAGS) \
	$(LIBZ_CFLAGS) \
	$(LIBNFS_CFLAGS) \
	-DSPLICE \
	-D_GNU_SOURCE
AM_LDADD = \
	-lrt \
	$(GLIB_LIBS) \
	$(LIBEVENT_LIBS)
AM_LDFLAGS += -pthread

if DEFLATE
else
AM_CPPFLAGS += -DNO_DEFLATE
endif


#
# Source files
#

if ENABLE_STOPWATCH
STOPWATCH_SRC = \
	src/istream-stopwatch.c \
	src/stopwatch.c src/stopwatch.h
else
STOPWATCH_SRC =
endif

#
# Programs
#

bin_PROGRAMS = src/cm4all-beng-proxy \
	src/cm4all-beng-lb \
	src/cm4all-beng-proxy-log-cat \
	src/cm4all-beng-proxy-log-traffic \
	src/cm4all-beng-proxy-log-split \
	src/cm4all-beng-proxy-log-forward \
	src/cm4all-beng-proxy-log-exec \
	src/cm4all-beng-proxy-delegate-helper

src_cm4all_beng_proxy_SOURCES = \
	include/beng-proxy/headers.h \
	include/beng-proxy/translation.h \
	src/product.h \
	src/capabilities.cxx src/capabilities.hxx \
	src/gerrno.h \
	src/glibfwd.hxx \
	src/abort_close.cxx src/abort_close.hxx \
	src/abort_flag.hxx \
	src/abort_unref.cxx src/abort_unref.hxx \
	src/access_log.cxx src/access_log.hxx \
	src/address.c src/address.h \
	src/ajp_client.cxx src/ajp_client.hxx \
	src/ajp_headers.cxx src/ajp_headers.hxx \
	src/ajp-protocol.c src/ajp-protocol.h \
	src/ajp-read.h \
	src/ajp_request.cxx src/ajp_request.hxx \
	src/ajp_serialize.cxx src/ajp_serialize.hxx \
	src/ajp-util.h \
	src/args.cxx src/args.hxx \
	src/background.hxx \
	src/balancer.cxx src/balancer.hxx \
	src/bot.c src/bot.h \
	src/buffered_io.c src/buffered_io.h \
	src/bulldog.c src/bulldog.h \
	src/cache.cxx src/cache.hxx \
	src/cgi_glue.cxx src/cgi_glue.hxx \
	src/cgi_quark.h \
	src/cgi_parser.cxx src/cgi_parser.hxx \
	src/cgi_client.cxx src/cgi_client.hxx \
	src/cgi_launch.cxx src/cgi_launch.hxx \
	src/pivot_root.h \
	src/child_manager.cxx src/child_manager.hxx \
	src/child_socket.cxx src/child_socket.hxx \
	src/child_stock.cxx src/child_stock.hxx \
	src/config.hxx \
	src/bp_listener.cxx src/bp_listener.hxx \
	src/bp_connection.cxx src/bp_connection.hxx \
	src/dchunk.h \
	src/defer.cxx src/defer.hxx \
	src/defer_event.h \
	src/delegate_client.cxx src/delegate_client.hxx \
	src/delegate_glue.cxx src/delegate_glue.hxx \
	src/delegate_protocol.h \
	src/delegate_request.cxx src/delegate_request.hxx \
	src/delegate_stock.cxx src/delegate_stock.hxx \
	src/delegate_handler.cxx \
	src/dhashmap.h \
	src/direct.c src/direct.h \
	src/dpool.c src/dpool.h \
	src/drop.cxx src/drop.hxx \
	src/exec.cxx src/exec.hxx \
	src/expiry.h \
	src/failure.cxx src/failure.hxx \
	src/fcache.cxx src/fcache.h \
	src/fcgi_client.cxx src/fcgi_client.hxx \
	src/fcgi_launch.cxx src/fcgi_launch.hxx \
	src/fcgi_remote.cxx src/fcgi_remote.hxx \
	src/fcgi_protocol.h \
	src/fcgi_request.cxx src/fcgi_request.hxx \
	src/fcgi_serialize.cxx src/fcgi_serialize.hxx \
	src/fcgi_stock.cxx src/fcgi_stock.hxx \
	src/was_control.cxx src/was_control.hxx \
	src/was_output.cxx src/was_output.hxx \
	src/was_input.cxx src/was_input.hxx \
	src/was_client.cxx src/was_client.hxx \
	src/was_stock.cxx src/was_stock.hxx \
	src/was_launch.cxx src/was_launch.hxx \
	src/was_glue.cxx src/was_glue.hxx \
	src/fd-util.c src/fd-util.h \
	src/fd_util.c src/fd_util.h \
	src/file_headers.cxx src/file_headers.hxx \
	src/file_handler.cxx src/file_handler.hxx \
	src/fork.cxx src/fork.hxx \
	src/get.cxx src/get.hxx \
	src/global.c src/global.h \
	src/handler.cxx src/handler.hxx \
	src/auth.cxx \
	src/file_not_found.cxx src/file_not_found.hxx \
	src/file_enotdir.cxx src/file_enotdir.hxx \
	src/file_directory_index.cxx src/file_directory_index.hxx \
	src/header_forward.cxx src/header_forward.hxx \
	src/header_parser.cxx src/header_parser.hxx \
	src/header_writer.cxx src/header_writer.hxx \
	src/header_copy.cxx src/header_copy.hxx \
	src/hostname.c src/hostname.h \
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
	src/http_server_internal.hxx \
	src/http_server.cxx src/http_server.hxx \
	src/http_server_send.cxx \
	src/http_server_request.cxx \
	src/http_server_read.cxx \
	src/http_server_response.cxx \
	src/http_string.cxx src/http_string.hxx \
	src/http_upgrade.cxx src/http_upgrade.hxx \
	src/http_util.cxx src/http_util.hxx \
	src/http_headers.hxx \
	src/http_quark.h \
	src/http_response.cxx src/http_response.hxx \
	src/control_server.cxx src/control_server.hxx \
	src/control_local.cxx src/control_local.hxx \
	src/bp_stats.cxx src/bp_stats.hxx \
	src/bp_control.cxx src/bp_control.hxx \
	src/lock.h \
	src/memcached_client.cxx src/memcached_client.hxx \
	src/memcached_packet.hxx \
	src/memcached_protocol.hxx \
	src/memcached_stock.cxx src/memcached_stock.hxx \
	src/memcached_packet.cxx \
	src/xml_parser.cxx src/xml_parser.hxx \
	src/pevent.h \
	src/pipe_stock.cxx src/pipe_stock.hxx \
	src/pipe_filter.cxx src/pipe_filter.hxx \
	src/tpool.c \
	src/processor.cxx src/processor.h \
	src/penv.cxx src/penv.hxx \
	src/proxy_widget.cxx src/proxy_widget.hxx \
	src/random.c src/random.h \
	src/refcount.h \
	src/request_forward.cxx src/request_forward.hxx \
	src/request.cxx src/request.hxx \
	src/request_session.cxx \
	src/rerror.cxx \
	src/response.cxx \
	src/generate_response.cxx src/generate_response.hxx \
	src/resource_tag.cxx src/resource_tag.hxx \
	src/rewrite_uri.cxx src/rewrite_uri.hxx \
	src/rwlock.h \
	src/serialize.cxx src/serialize.hxx \
	src/shm.c src/shm.h \
	src/sink_gstring.cxx src/sink_gstring.hxx \
	src/sink-impl.h \
	src/socket_wrapper.cxx src/socket_wrapper.hxx \
	src/buffered_socket.cxx src/buffered_socket.hxx \
	src/filtered_socket.cxx src/filtered_socket.hxx \
	src/nop_socket_filter.cxx src/nop_socket_filter.hxx \
	src/file_request.cxx src/file_request.hxx \
	src/static_headers.cxx src/static_headers.hxx \
	src/stock.cxx src/stock.hxx \
	src/hstock.cxx src/hstock.hxx \
	src/mstock.cxx src/mstock.h \
	src/strref-dpool.h \
	src/suffix_registry.cxx src/suffix_registry.hxx \
	src/tcache.cxx src/tcache.hxx \
	src/tcp_stock.cxx src/tcp_stock.hxx \
	src/tcp_balancer.cxx src/tcp_balancer.hxx \
	src/tpool.h \
	src/transformation.cxx src/transformation.hxx \
	src/tstock.cxx src/tstock.hxx \
	src/udp-distribute.h \
	src/widget.hxx \
	$(STOPWATCH_SRC) \
	src/clock.c src/clock.h \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/bp_cmdline.cxx \
	src/completion.h \
	src/crash.h \
	src/crash.c \
	src/bp_worker.cxx src/worker.h \
	src/dstring.c \
	src/dbuffer.hxx \
	src/istream_rubber.cxx src/istream_rubber.hxx \
	src/sink_rubber.cxx src/sink_rubber.hxx \
	src/session.cxx src/session.hxx \
	src/session_id.c src/session_id.h \
	src/session_manager.cxx src/session_manager.hxx \
	src/session_write.cxx src/session_write.hxx \
	src/session_read.cxx src/session_read.hxx \
	src/session_save.cxx src/session_save.hxx \
	src/cookie_jar.cxx src/cookie_jar.hxx \
	src/cookie_client.cxx src/cookie_client.hxx \
	src/cookie_server.cxx src/cookie_server.hxx \
	src/cookie_string.cxx src/cookie_string.hxx \
	src/translate_client.cxx src/translate_client.hxx \
	src/translate_request.hxx \
	src/translate_response.cxx rc/translate_response.hxx \
	src/tvary.cxx src/tvary.hxx \
	src/lhttp_quark.hxx \
	src/lhttp_launch.cxx src/lhttp_launch.hxx \
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
	src/widget-quark.h \
	src/widget_request.cxx src/widget_request.hxx \
	src/widget_registry.cxx src/widget_registry.hxx \
	src/widget_resolver.cxx src/widget_resolver.hxx \
	src/widget_approval.cxx src/widget_approval.hxx \
	src/widget_dump.cxx src/widget_dump.hxx \
	src/escape_pool.c src/escape_pool.h \
	src/istream-escape.c src/istream-escape.h \
	src/istream-html-escape.c \
	src/pheaders.cxx src/pheaders.hxx \
	src/css_syntax.hxx \
	src/css_util.hxx \
	src/css_parser.cxx src/css_parser.hxx \
	src/css_processor.cxx src/css_processor.h \
	src/css_rewrite.cxx src/css_rewrite.hxx \
	src/text_processor.cxx src/text_processor.hxx \
	src/resource_loader.cxx src/resource_loader.hxx \
	src/widget_http.cxx src/widget_http.hxx \
	src/inline_widget.cxx src/inline_widget.hxx \
	src/frame.cxx src/frame.hxx \
	src/udp_listener.cxx src/udp_listener.hxx \
	src/udp-distribute.c \
	src/istream-ajp-body.c \
	src/istream_gb.cxx src/istream_gb.hxx \
	src/sigutil.h \
	src/address_quark.h \
	src/address_resolver.cxx src/address_resolver.hxx \
	src/address_string.cxx src/address_string.hxx \
	src/dhashmap.c \
	src/istream_unlock.cxx \
	src/ua_classification.c src/ua_classification.h \
	src/log-client.c src/log-client.h \
	src/log-launch.c src/log-launch.h \
	src/log-glue.c src/log-glue.h \
	src/notify.cxx src/notify.hxx \
	src/thread_job.h \
	src/thread_queue.cxx src/thread_queue.hxx \
	src/thread_worker.cxx src/thread_worker.hxx \
	src/thread_pool.cxx src/thread_pool.hxx \
	src/ssl_domain.cxx src/ssl_domain.hxx \
	src/ssl_init.cxx src/ssl_init.hxx \
	src/ssl_client.cxx src/ssl_client.hxx \
	src/ssl_factory.cxx src/ssl_factory.hxx \
	src/ssl_filter.cxx src/ssl_filter.hxx \
	src/thread_socket_filter.cxx src/thread_socket_filter.hxx \
	src/bp_instance.cxx src/bp_instance.hxx \
	src/shutdown_listener.c src/shutdown_listener.h \
	src/bp_main.cxx
src_cm4all_beng_proxy_LDADD = $(AM_LDADD) \
	-lcap \
	libistream.a libpool.a libmemory.a \
	libnet.a \
	libraddress.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_INLINE_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBATTR_LIBS) \
	$(LIBNFS_LIBS) \
	$(LIBSSL_LIBS) \
	$(LIBZ_LIBS)

if HAVE_LIBNFS
src_cm4all_beng_proxy_SOURCES += \
	src/nfs_handler.cxx src/nfs_handler.hxx \
	src/istream_nfs.cxx src/istream_nfs.hxx \
	src/nfs_stock.cxx src/nfs_stock.hxx \
	src/nfs_client.cxx src/nfs_client.hxx \
	src/nfs_cache.cxx src/nfs_cache.hxx \
	src/nfs_request.cxx src/nfs_request.hxx
endif

src_cm4all_beng_lb_SOURCES = \
	src/isolate.cxx src/isolate.hxx \
	src/abort_close.cxx src/abort_close.hxx \
	src/capabilities.cxx src/capabilities.hxx \
	src/clock.c src/clock.h \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/tpool.c \
	src/stock.cxx src/stock.hxx \
	src/hstock.cxx \
	src/tcp_stock.cxx \
	src/tcp_balancer.cxx \
	src/pipe_stock.cxx \
	src/failure.cxx \
	src/balancer.cxx \
	src/address.c \
	src/address_list.cxx \
	src/address_envelope.hxx \
	src/address_resolver.cxx src/address_resolver.hxx \
	src/address_string.cxx src/address_string.hxx \
	src/address_edit.h \
	src/address_edit.c \
	src/address_sticky.c src/address_sticky.h \
	src/client_balancer.cxx src/client_balancer.hxx \
	src/bulldog.c \
	src/child_manager.cxx src/child_manager.hxx \
	src/cache.cxx \
	src/http_body.cxx \
	src/http_server.cxx \
	src/http_server_send.cxx \
	src/http_server_request.cxx \
	src/http_server_read.cxx \
	src/http_server_response.cxx \
	src/http_client.cxx \
	src/http_string.cxx \
	src/http_upgrade.cxx src/http_upgrade.hxx \
	src/http_util.cxx src/http_util.hxx \
	src/header_writer.cxx \
	src/header_parser.cxx \
	src/regex.cxx src/regex.hxx \
	src/http_address.cxx \
	src/istream_gb.cxx \
	src/buffered_io.c \
	src/fd_util.c \
	src/fd-util.c \
	$(STOPWATCH_SRC) \
	src/udp_listener.cxx src/udp_listener.hxx \
	src/control_server.cxx src/control_server.hxx \
	src/direct.c \
	src/access_log.cxx src/access_log.hxx \
	src/cookie_server.cxx \
	src/cookie_string.cxx \
	src/log-glue.c \
	src/log-client.c \
	src/log-launch.c \
	src/sticky.h \
	src/notify.cxx src/notify.hxx \
	src/thread_job.h \
	src/thread_queue.cxx src/thread_queue.hxx \
	src/thread_worker.cxx src/thread_worker.hxx \
	src/thread_pool.cxx src/thread_pool.hxx \
	src/ssl_quark.hxx \
	src/ssl_domain.cxx src/ssl_domain.hxx \
	src/ssl_init.hxx \
	src/ssl_init.cxx \
	src/ssl_config.hxx \
	src/ssl_factory.hxx \
	src/ssl_factory.cxx \
	src/ssl_filter.hxx \
	src/ssl_filter.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/thread_socket_filter.cxx src/thread_socket_filter.hxx \
	src/nop_thread_socket_filter.cxx src/nop_thread_socket_filter.hxx \
	src/ping.cxx src/ping.hxx \
	src/shutdown_listener.c src/shutdown_listener.h \
	src/lb_global.cxx \
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
	src/lb_main.cxx
src_cm4all_beng_lb_LDADD = $(AM_LDADD) \
	-lcap \
	libistream.a libpool.a libmemory.a \
	libnet.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_INLINE_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIBSSL_LIBS) \
	$(LIBEVENT_LIBS)

src_cm4all_beng_proxy_log_cat_SOURCES = \
	src/log-server.c \
	src/log-cat.c
src_cm4all_beng_proxy_log_cat_LDADD = \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

src_cm4all_beng_proxy_log_traffic_SOURCES = \
	src/log-server.c \
	src/log-traffic.c
src_cm4all_beng_proxy_log_traffic_LDADD = \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

src_cm4all_beng_proxy_log_split_SOURCES = \
	src/log-server.c \
	src/log-split.c
src_cm4all_beng_proxy_log_split_LDADD = \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

src_cm4all_beng_proxy_log_forward_SOURCES = \
	src/fd_util.c \
	src/log-forward.c
src_cm4all_beng_proxy_log_forward_LDADD = \
	$(LIBCM4ALL_SOCKET_LIBS)

src_cm4all_beng_proxy_log_exec_SOURCES = \
	src/log-exec.c
src_cm4all_beng_proxy_log_exec_LDADD = \
	$(LIBCM4ALL_SOCKET_LIBS)

src_cm4all_beng_proxy_delegate_helper_SOURCES = src/delegate_helper.c

# LLVM

if LLVM

llvmdir = $(libdir)/llvm
llvm_DATA = cm4all-beng-proxy.bc

src_cm4all_beng_proxy_CSOURCES = $(filter-out %.h,$(src_cm4all_beng_proxy_SOURCES))
src_cm4all_beng_proxy_BC = $(src_cm4all_beng_proxy_CSOURCES:.c=.bc)

$(src_cm4all_beng_proxy_BC): %.bc: %.c
	$(CLANG) -c -o $@ $< -emit-llvm $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)

cm4all-beng-proxy-unoptimized.bc: $(src_cm4all_beng_proxy_BC)
	$(LLVM_LINK) -o $@ $^

cm4all-beng-proxy.bc: cm4all-beng-proxy-unoptimized.bc
	$(LLVM_OPT) -o $@ $< -std-compile-opts -std-link-opts -O3

cm4all-beng-proxy.s: cm4all-beng-proxy.bc
	$(LLVM_LLC) -o $@ $< -O3

cm4all-beng-proxy: cm4all-beng-proxy.s
	$(CLANG) -o $@ $^ $(src_cm4all_beng_proxy_LDFLAGS) $(src_cm4all_beng_proxy_LDADD)

src_cm4all_beng_lb_CSOURCES = $(filter-out %.h,$(src_cm4all_beng_lb_SOURCES))
src_cm4all_beng_lb_BC = $(src_cm4all_beng_lb_CSOURCES:.c=.bc)

$(src_cm4all_beng_lb_BC): %.bc: %.c
	$(CLANG) -c -o $@ $< -emit-llvm $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)

cm4all-beng-lb-unoptimized.bc: $(src_cm4all_beng_lb_BC)
	$(LLVM_LINK) -o $@ $^

cm4all-beng-lb.bc: cm4all-beng-lb-unoptimized.bc
	$(LLVM_OPT) -o $@ $< -std-compile-opts -std-link-opts -O3

cm4all-beng-lb.s: cm4all-beng-lb.bc
	$(LLVM_LLC) -o $@ $< -O3

cm4all-beng-lb: cm4all-beng-lb.s
	$(CLANG) -o $@ $^ $(src_cm4all_beng_lb_LDFLAGS) $(src_cm4all_beng_lb_LDADD)

CLEANFILES = \
	cm4all-beng-lb-unoptimized.bc cm4all-beng-lb.bc cm4all-beng-lb.s cm4all-beng-lb $(src_cm4all_beng_lb_BC) \
	cm4all-beng-proxy-unoptimized.bc cm4all-beng-proxy.bc cm4all-beng-proxy.s cm4all-beng-proxy $(src_cm4all_beng_proxy_BC)

endif


#
# Libraries
#

noinst_LIBRARIES = libutil.a libmemory.a libpool.a \
	libnet.a \
	libputil.a \
	libraddress.a \
	libistream.a

libutil_a_SOURCES = \
	src/util/Cast.hxx \
	src/util/ConstBuffer.hxx \
	src/util/DereferenceIterator.hxx \
	src/util/TrivialArray.hxx \
	src/util/StaticArray.hxx \
	src/util/Domain.hxx \
	src/util/Error.cxx src/util/Error.hxx \
	src/util/djbhash.c src/util/djbhash.h \
	src/kvlist.hxx \
	src/format.c format.h \
	src/date.c src/date.h \
	src/gmtime.c src/gmtime.h \
	src/html-chars.h \
	src/async.hxx \
	src/lease.hxx \
	src/strref.h \
	src/strref2.h \
	src/strutil.c src/strutil.h \
	src/trace.h \
	src/uri-parser.c src/uri-parser.h \
	src/uri-verify.c src/uri-verify.h \
	src/uri-string.h \
	src/uset.h \
	src/crc.h \
	src/escape_class.h \
	src/escape_html.c src/escape_html.h \
	src/escape_css.c src/escape_css.h

libputil_a_SOURCES = \
	src/expansible-buffer.c src/expansible-buffer.h \
	src/fifo-buffer.c src/fifo-buffer.h \
	src/growing_buffer.cxx src/growing_buffer.hxx \
	src/hashmap.cxx src/hashmap.hxx \
	src/please.hxx \
	src/strmap.cxx src/strmap.hxx \
	src/strref-pool.h \
	src/strset.c src/strset.h \
	src/uri_base.cxx src/uri_base.hxx \
	src/uri-edit.c src/uri-edit.h \
	src/uri_escape.cxx src/uri_escape.hxx \
	src/uri-extract.c src/uri-extract.h \
	src/uri-relative.c src/uri-relative.h

libmemory_a_SOURCES = \
	src/mmap.h \
	src/fb_pool.cxx src/fb_pool.hxx \
	src/slice.cxx src/slice.hxx \
	src/rubber.cxx src/rubber.hxx

libpool_a_SOURCES = \
	src/pbuffer.hxx \
	src/pstring.c \
	src/pool.cxx src/pool.hxx src/pool.h

libnet_a_SOURCES = \
	src/net/Error.cxx src/net/Error.hxx \
	src/net/ConnectSocket.cxx src/net/ConnectSocket.hxx \
	src/net/ServerSocket.cxx src/net/ServerSocket.hxx \
	src/net/StaticSocketAddress.cxx src/net/StaticSocketAddress.hxx \
	src/net/AllocatedSocketAddress.cxx src/net/AllocatedSocketAddress.hxx \
	src/net/SocketDescriptor.cxx src/net/SocketDescriptor.hxx

libraddress_a_SOURCES = \
	src/regex.cxx src/regex.hxx \
	src/param_array.hxx src/param_array.cxx \
	src/namespace_options.cxx src/namespace_options.hxx \
	src/mount_list.c src/mount_list.h \
	src/bind_mount.c src/bind_mount.h \
	src/jail.cxx src/jail.hxx \
	src/child_options.cxx src/child_options.hxx \
	src/rlimit_options.cxx src/rlimit_options.hxx \
	src/cgi_address.cxx src/cgi_address.hxx \
	src/file_address.cxx src/file_address.hxx \
	src/nfs_address.cxx src/nfs_address.hxx \
	src/lhttp_address.cxx src/lhttp_address.hxx \
	src/http_address.cxx src/http_address.hxx \
	src/address_list.cxx src/address_list.hxx \
	src/resource_address.cxx src/resource_address.hxx

libistream_a_SOURCES = \
	src/istream_pause.c src/istream_pause.h \
	src/istream-buffer.h \
	src/istream-impl.h \
	src/istream-internal.h \
	src/istream-invoke.h \
	src/istream-new.h \
	src/istream-direct.h \
	src/istream.cxx src/istream.h \
	src/istream-forward.c src/istream-forward.h \
	src/istream-memory.c \
	src/istream-null.c \
	src/istream-zero.c \
	src/istream-block.c \
	src/istream-string.c \
	src/istream_file.cxx src/istream_file.hxx \
	src/istream-chunked.c \
	src/istream-dechunk.c \
	src/istream_fcgi.c \
	src/istream-cat.c \
	src/istream_pipe.cxx \
	src/istream_delayed.cxx \
	src/istream-hold.c \
	src/istream-optional.c \
	src/istream-deflate.c \
	src/istream-subst.c \
	src/istream-byte.c \
	src/istream-four.c \
	src/istream-iconv.c \
	src/istream-trace.c \
	src/istream-fail.c \
	src/istream-inject.c \
	src/istream-catch.c istream-catch.h \
	src/istream-later.c \
	src/istream-head.c \
	src/istream_tee.c src/istream_tee.h \
	src/istream_replace.cxx src/istream_replace.hxx \
	src/istream_socket.cxx src/istream_socket.hxx \
	src/istream-socketpair.c \
	src/istream-notify.h \
	src/istream-notify.c \
	src/sink-null.c \
	src/sink_buffer.cxx \
	src/sink_header.cxx \
	src/sink_fd.c src/sink_fd.h

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
	test/run_css_parser \
	test/run_ping \
	test/run_was \
	test/was_mirror \
	test/benchmark-gmtime test/format-http-date \
	test/request_translation \
	test/run_subst \
	test/run_cookie_client \
	test/run_cookie_server \
	test/run_header_parser \
	test/run-html-unescape test/run-html-escape \
	test/run_processor \
	test/run_client_balancer \
	test/run_client \
	test/run_memcached_client \
	test/run_http_cache \
	test/dump_memcached_choice \
	test/cleanup_memcached_choice \
	test/fake-memcached-server \
	test/run_delegate \
	test/dump_udp test/dump_control test/send-control \
	test/run_ua_parser \
	test/run_parser_cdata

if HAVE_LIBNFS
noinst_PROGRAMS += test/run_nfs_client
endif

FILTER_TESTS = test/t_istream_cat test/t_istream_chunked test/t_istream_dechunk \
	test/t_istream_pipe test/t_istream_hold test/t_istream_delayed \
	test/t_istream_subst test/t_istream_deflate test/t_istream_byte \
	test/t_istream_iconv test/t_istream_replace test/t_istream_replace2 \
	test/t_istream_html_escape test/t_istream_fcgi \
	test/t_sink_header \
	test/t_sink_header_empty \
	test/t_istream_processor

check_PROGRAMS = \
	test/t_hashmap test/t_cache test/t_tcache \
	test/t_header_forward \
	test/t_stock \
	test/t_resource_address \
	test/t_http_address \
	test/t_cgi_address \
	test/t-uri-verify \
	test/t_uri_relative \
	test/t-html-escape \
	test/t-escape-css \
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
	test/t_cookie_client \
	test/t_cookie_server \
	test/t_pool \
	test/t-shm test/t-dpool test/t_session \
	test/t_rubber test/t_sink_rubber \
	test/t_slice \
	test/t_rewrite_uri \
	test/t_widget_registry \
	test/t_widget_resolver \
	test/t_wembed test/t_widget_http \
	test/t_processor \
	test/t_memcached_client

TESTS = test/t_hashmap test/t_cache test/t_tcache \
	test/t_header_forward \
	test/t_stock \
	test/t_resource_address \
	test/t_http_address \
	test/t_cgi_address \
	test/t-uri-verify \
	test/t_uri_relative \
	test/t-html-escape \
	test/t-escape-css \
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
	test/t_processor \
	test/t_memcached_client \
	test/t_cookie_client.py test/t_cookie_client \
	test/t_cookie_server \
	test/t-shm test/t-dpool test/t_session \
	test/t_pool \
	test/t_rubber test/t_sink_rubber \
	test/t_slice \
	test/t_rewrite_uri \
	test/t_widget_registry \
	test/t_widget_resolver \
	test/t_wembed test/t_widget_http

test_run_ping_SOURCES = test/run_ping.cxx \
	src/ping.cxx \
	src/address_string.cxx
test_run_ping_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libnet.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

test_run_was_SOURCES = test/run_was.cxx \
	src/was_launch.cxx src/was_client.cxx \
	src/was_control.cxx src/was_input.cxx src/was_output.cxx \
	src/exec.cxx \
	src/buffered_io.c \
	src/cleanup_timer.c \
	src/direct.c \
	src/fd_util.c src/fd-util.c
test_run_was_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libputil.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

test_was_mirror_SOURCES = test/was_mirror.cxx \
	src/was_server.cxx src/was_server.hxx \
	src/was_control.cxx src/was_input.cxx src/was_output.cxx \
	src/buffered_io.c \
	src/direct.c \
	src/fd_util.c src/fd-util.c
test_was_mirror_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

test_t_hashmap_SOURCES = test/t_hashmap.cxx
test_t_hashmap_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_cache_SOURCES = test/t_cache.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/clock.c \
	src/cache.cxx
test_t_cache_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_t_tcache_SOURCES = test/t_tcache.cxx src/tcache.cxx src/cache.cxx \
	src/translate_response.cxx \
	src/exec.cxx \
	src/widget_view.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/clock.c \
	src/tpool.c \
	src/transformation.cxx
test_t_tcache_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) -lrt

test_t_header_forward_SOURCES = test/t_header_forward.cxx \
	src/header_forward.cxx src/header_writer.cxx \
	src/header_copy.cxx \
	src/http_string.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx \
	src/cookie_server.cxx \
	src/cookie_string.cxx \
	src/shm.c src/dpool.c src/dstring.c src/dhashmap.c \
	src/random.c src/fd-util.c \
	src/fd_util.c \
	src/crash.c \
	src/session_id.c \
	src/clock.c \
	src/tpool.c
test_t_header_forward_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS)

test_t_stock_SOURCES = test/t_stock.cxx src/stock.cxx
test_t_stock_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) $(GLIB_LIBS)

test_t_resource_address_SOURCES = test/t_resource_address.cxx \
	src/regex.cxx \
	src/http_address.cxx \
	src/exec.cxx \
	src/address_list.cxx
test_t_resource_address_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(GLIB_LIBS) $(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS)

test_t_http_address_SOURCES = test/t_http_address.cxx \
	src/http_address.cxx \
	src/regex.cxx \
	src/address_list.cxx
test_t_http_address_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(GLIB_LIBS) $(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS)

test_t_cgi_address_SOURCES = test/t_cgi_address.cxx \
	src/regex.cxx \
	src/exec.cxx \
	src/jail.cxx
test_t_cgi_address_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_cgi_address_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(CPPUNIT_LIBS)

test_t_uri_verify_SOURCES = test/t-uri-verify.c
test_t_uri_verify_LDADD = $(AM_LDADD) libutil.a

test_t_uri_relative_SOURCES = test/t_uri_relative.cxx
test_t_uri_relative_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_expansible_buffer_SOURCES = test/t_expansible_buffer.cxx
test_t_expansible_buffer_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS)

T_ISTREAM_FILTER_SRC = \
	src/direct.c \
	src/buffered_io.c \
	src/pipe_stock.cxx src/stock.cxx \
	src/fd_util.c \
	src/fd-util.c
T_ISTREAM_FILTER_LIB = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) $(LIBZ_LIBS)

test_t_istream_cat_SOURCES = test/t_istream_cat.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_cat_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_chunked_SOURCES = test/t_istream_chunked.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_chunked_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_dechunk_SOURCES = test/t_istream_dechunk.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_dechunk_LDADD = $(T_ISTREAM_FILTER_LIB)

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
	src/istream-escape.c \
	src/istream-html-escape.c \
	test/t_istream_html_escape.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_html_escape_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_fcgi_SOURCES = test/t_istream_cat.cxx $(T_ISTREAM_FILTER_SRC)
test_t_istream_fcgi_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_processor_SOURCES = test/t_istream_processor.cxx \
	src/istream-escape.c \
	src/istream-html-escape.c \
	src/exec.cxx \
	src/random.c \
	src/crash.c \
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
	src/tpool.c src/rewrite_uri.cxx src/widget_resolver.cxx \
	src/dhashmap.c src/dpool.c src/shm.c src/dstring.c \
	src/global.c src/transformation.cxx \
	src/clock.c \
	$(T_ISTREAM_FILTER_SRC)
test_t_istream_processor_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libutil.a \
	$(T_ISTREAM_FILTER_LIB) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_sink_header_SOURCES = test/t_sink_header.cxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_sink_header_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_sink_header_empty_SOURCES = test/t_sink_header_empty.cxx \
	$(T_ISTREAM_FILTER_SRC)
test_t_sink_header_empty_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_tee_SOURCES = test/t_istream_tee.cxx \
	src/sink_gstring.cxx \
	src/sink-close.c \
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
	src/address_list.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/clock.c
test_t_balancer_CPPFLAGS = $(AM_CPPFLAGS) $(CPPUNIT_CFLAGS)
test_t_balancer_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(CPPUNIT_LIBS)

test_t_cgi_SOURCES = test/t_cgi.cxx \
	$(STOPWATCH_SRC) \
	src/clock.c \
	src/cgi_glue.cxx src/fork.cxx \
	src/cgi_client.cxx src/cgi_launch.cxx \
	src/cgi_parser.cxx \
	src/exec.cxx \
	src/cleanup_timer.c \
	src/regex.cxx \
	src/tpool.c \
	src/buffered_io.c \
	src/header_parser.cxx \
	src/fd_util.c src/fd-util.c \
	src/child_manager.cxx src/child_manager.hxx \
	src/crash.c \
	src/direct.c
test_t_cgi_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS)

test_t_http_server_SOURCES = test/t_http_server.cxx \
	src/http_util.cxx \
	src/http_server.cxx src/http_server_send.cxx src/http_server_request.cxx \
	src/http_server_read.cxx src/http_server_response.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/cleanup_timer.c \
	src/address.c \
	src/buffered_io.c \
	src/header_writer.cxx \
	src/istream_gb.cxx \
	src/pipe_stock.cxx src/stock.cxx \
	src/direct.c \
	src/http_body.cxx \
	src/fd_util.c src/fd-util.c \
	src/header_parser.cxx \
	src/tpool.c
test_t_http_server_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_http_server_SOURCES = test/run_http_server.cxx \
	src/http_util.cxx \
	src/http_server.cxx src/http_server_send.cxx src/http_server_request.cxx \
	src/http_server_read.cxx src/http_server_response.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/address.c \
	src/duplex.c \
	src/buffered_io.c \
	src/header_writer.cxx \
	src/istream_gb.cxx \
	src/pipe_stock.cxx src/stock.cxx \
	src/direct.c \
	src/http_body.cxx \
	src/fd_util.c src/fd-util.c \
	src/cleanup_timer.c \
	src/shutdown_listener.c \
	src/header_parser.cxx \
	src/tpool.c src/event2.c
test_run_http_server_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_http_client_SOURCES = test/t_http_client.cxx \
	src/t_client.hxx \
	src/http_client.cxx \
	src/http_util.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/cleanup_timer.c \
	$(STOPWATCH_SRC) \
	src/header_writer.cxx \
	src/istream_gb.cxx \
	src/direct.c \
	src/http_body.cxx src/header_parser.cxx \
	src/buffered_io.c src/fd_util.c src/fd-util.c \
	src/tpool.c
test_t_http_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
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
	src/tpool.c \
	src/sink_rubber.cxx \
	src/exec.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/header_parser.cxx \
	src/istream_rubber.cxx \
	src/istream_gb.cxx \
	src/http_cache.cxx src/http_cache_rfc.cxx \
	src/http_cache_info.cxx src/http_cache_document.cxx \
	src/http_cache_heap.cxx src/http_cache_age.cxx \
	src/abort_unref.cxx src/cache.cxx \
	src/header_writer.cxx src/http_util.cxx \
	src/clock.c \
	src/istream_unlock.cxx
test_t_http_cache_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

test_t_cookie_client_SOURCES = test/t_cookie_client.cxx \
	src/tpool.c \
	src/clock.c \
	src/cookie_client.cxx src/cookie_jar.cxx \
	src/cookie_string.cxx \
	src/http_string.cxx \
	src/header_writer.cxx \
	src/shm.c src/dpool.c src/dstring.c
test_t_cookie_client_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
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

test_t_shm_SOURCES = test/t-shm.c src/shm.c
test_t_shm_LDADD = $(AM_LDADD) $(LIBCM4ALL_DAEMON_LIBS)

test_t_dpool_SOURCES = test/t-dpool.c src/shm.c src/dpool.c
test_t_dpool_LDADD = $(AM_LDADD) $(LIBCM4ALL_DAEMON_LIBS)

test_t_session_SOURCES = test/t_session.cxx src/shm.c \
	src/dpool.c src/dstring.c src/dhashmap.c \
	src/clock.c \
	src/random.c src/fd-util.c src/fd_util.c \
	src/crash.c \
	src/session.cxx src/session_manager.cxx
test_t_session_LDADD = $(AM_LDADD) \
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

test_t_rewrite_uri_SOURCES = test/t_rewrite_uri.cxx \
	src/tpool.c \
	src/penv.cxx \
	src/exec.cxx \
	src/escape_pool.c \
	src/istream-escape.c \
	src/istream-html-escape.c \
	src/rewrite_uri.cxx src/widget_uri.cxx src/widget.cxx \
	src/widget_init.cxx src/widget_view.cxx src/transformation.cxx \
	src/sink_gstring.cxx \
	src/args.cxx
test_t_rewrite_uri_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_widget_registry_SOURCES = test/t_widget_registry.cxx \
	src/widget_registry.cxx src/stock.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/transformation.cxx \
	src/tcache.cxx src/cache.cxx \
	src/translate_response.cxx \
	src/widget_view.cxx \
	src/exec.cxx \
	src/abort_unref.cxx \
	src/tpool.c \
	src/clock.c
test_t_widget_registry_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_widget_resolver_SOURCES = test/t_widget_resolver.cxx \
	src/exec.cxx \
	src/widget_resolver.cxx \
	src/widget_init.cxx
test_t_widget_resolver_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS) $(GLIB_LIBS)

test_t_wembed_SOURCES = test/t_wembed.cxx \
	src/widget_init.cxx \
	src/widget_approval.cxx \
	src/inline_widget.cxx \
	src/istream-escape.c src/istream-html-escape.c \
	src/http_util.cxx \
	src/global.c
test_t_wembed_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(GLIB_LIBS)

test_t_widget_http_SOURCES = test/t_widget_http.cxx \
	src/widget_http.cxx src/widget_uri.cxx src/widget_request.cxx \
	src/pheaders.cxx \
	src/exec.cxx \
	src/crash.c \
	src/widget_session.cxx src/session.cxx src/session_manager.cxx src/session_id.c \
	src/widget_view.cxx src/widget.cxx src/widget_init.cxx \
	src/random.c src/fd-util.c \
	src/fd_util.c \
	src/args.cxx \
	src/resource_tag.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx \
	src/cookie_server.cxx \
	src/cookie_string.cxx \
	src/header_parser.cxx src/header_forward.cxx src/header_writer.cxx \
	src/header_copy.cxx \
	src/http_string.cxx src/http_util.cxx \
	src/istream-escape.c \
	src/transformation.cxx \
	src/dpool.c src/dstring.c src/dhashmap.c src/shm.c \
	src/clock.c \
	src/tpool.c
test_t_widget_http_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_benchmark_gmtime_SOURCES = test/benchmark-gmtime.c \
	test/libcore-gmtime.c
test_benchmark_gmtime_LDADD = $(AM_LDADD) libutil.a

test_format_http_date_SOURCES = test/format-http-date.c
test_format_http_date_LDADD = $(AM_LDADD) libutil.a

test_request_translation_SOURCES = test/request_translation.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	c src/buffered_io.c src/direct.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/translate_client.cxx src/tstock.cxx \
	src/translate_response.cxx \
	src/failure.cxx src/bulldog.c src/balancer.cxx src/cache.cxx \
	src/tcp_stock.cxx \
	src/fd_util.c src/fd-util.c \
	src/stock.cxx src/hstock.cxx \
	src/abort_unref.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/exec.cxx \
	src/clock.c \
	$(STOPWATCH_SRC)
test_request_translation_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	libnet.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_subst_SOURCES = test/run_subst.cxx \
	c src/buffered_io.c \
	src/cleanup_timer.c \
	src/fd_util.c
test_run_subst_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_cookie_client_SOURCES = test/run_cookie_client.cxx \
	src/cookie_client.cxx src/cookie_jar.cxx src/http_string.cxx \
	src/cookie_string.cxx \
	src/header_writer.cxx \
	src/tpool.c \
	src/shm.c src/dpool.c src/dstring.c \
	src/clock.c
test_run_cookie_client_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
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
	src/header_parser.cxx \
	src/tpool.c
test_run_header_parser_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_html_unescape_SOURCES = \
	src/escape_static.c \
	test/run-html-unescape.c
test_run_html_unescape_LDADD = $(AM_LDADD) libutil.a

test_run_html_escape_SOURCES = \
	src/escape_static.c \
	test/run-html-escape.c
test_run_html_escape_LDADD = $(AM_LDADD) libutil.a

test_t_html_escape_SOURCES = \
	src/escape_static.c \
	test/t-html-escape.c
test_t_html_escape_LDADD = $(AM_LDADD) libutil.a

test_t_escape_css_SOURCES = \
	src/escape_static.c \
	test/t-escape-css.c
test_t_escape_css_LDADD = $(AM_LDADD) libutil.a

test_run_processor_SOURCES = test/run_processor.cxx \
	src/processor.cxx src/penv.cxx src/xml_parser.cxx \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.cxx src/css_rewrite.cxx \
	src/widget.cxx src/widget_init.cxx src/widget_approval.cxx src/widget_ref.cxx \
	src/tpool.c \
	src/istream-html-escape.c src/istream-escape.c \
	src/header_writer.cxx src/args.cxx src/buffered_io.c \
	src/fd_util.c \
	src/header_parser.cxx \
	src/widget_request.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/failure.cxx src/bulldog.c \
	src/balancer.cxx src/http_address.cxx src/cache.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/shm.c src/dpool.c src/dstring.c src/dhashmap.c \
	src/exec.cxx \
	src/clock.c \
	$(STOPWATCH_SRC)
test_run_processor_LDADD = $(AM_LDADD) \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_processor_SOURCES = test/t_processor.cxx \
	src/processor.cxx src/penv.cxx src/xml_parser.cxx \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.cxx src/css_rewrite.cxx \
	src/widget.cxx src/widget_init.cxx src/widget_approval.cxx src/widget_ref.cxx \
	src/exec.cxx \
	src/tpool.c \
	src/istream-html-escape.c src/istream-escape.c \
	src/header_writer.cxx src/args.cxx src/buffered_io.c \
	src/fd_util.c \
	src/header_parser.cxx \
	src/widget_request.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/failure.cxx src/bulldog.c \
	src/balancer.cxx src/cache.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/shm.c src/dpool.c src/dstring.c src/dhashmap.c \
	src/clock.c \
	$(STOPWATCH_SRC)
test_t_processor_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_client_balancer_SOURCES = test/run_client_balancer.cxx \
	src/address_list.cxx \
	src/client_balancer.cxx \
	src/balancer.cxx \
	src/failure.cxx \
	src/bulldog.c \
	src/cache.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/fd_util.c \
	src/clock.c \
	$(STOPWATCH_SRC)
test_run_client_balancer_LDADD = $(AM_LDADD) \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	libnet.a \
	libutil.a \
	$(LIBEVENT_LIBS) \
	-lrt \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

if HAVE_LIBNFS

test_run_nfs_client_SOURCES = test/run_nfs_client.cxx \
	src/nfs_client.cxx src/istream_nfs.cxx \
	src/sink_fd.c \
	src/direct.c \
	src/fd_util.c src/fd-util.c \
	src/stock.cxx src/pipe_stock.cxx \
	src/static_headers.cxx \
	src/shutdown_listener.c
test_run_nfs_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBNFS_LIBS) \
	$(GLIB_LIBS)
endif

test_t_fcgi_client_SOURCES = test/t_fcgi_client.cxx \
	src/fcgi_client.cxx src/fcgi_serialize.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/buffered_io.c \
	src/cleanup_timer.c \
	src/header_parser.cxx \
	src/istream_gb.cxx \
	src/fd-util.c src/fd_util.c \
	src/tpool.c \
	src/direct.c
test_t_fcgi_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_ajp_client_SOURCES = test/t_ajp_client.cxx \
	src/t_client.hxx src/tio.hxx \
	src/ajp_client.cxx src/ajp_serialize.cxx \
	src/ajp_headers.cxx src/ajp-protocol.c \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/cleanup_timer.c \
	src/serialize.cxx \
	src/buffered_io.c \
	src/fd-util.c src/fd_util.c \
	src/istream_gb.cxx \
	src/istream-ajp-body.c \
	src/direct.c 
test_t_ajp_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_client_SOURCES = test/run_client.cxx \
	src/ssl_domain.cxx src/ssl_domain.hxx \
	src/ssl_init.cxx \
	src/ssl_client.cxx \
	src/ssl_factory.cxx \
	src/ssl_filter.cxx \
	src/thread_socket_filter.cxx \
	src/thread_pool.cxx src/thread_queue.cxx src/thread_worker.cxx \
	src/notify.cxx \
	src/http_client.cxx src/http_body.cxx \
	src/header_writer.cxx src/header_parser.cxx \
	src/http_util.cxx \
	src/ajp_client.cxx src/ajp_serialize.cxx \
	src/ajp_headers.cxx src/ajp-protocol.c \
	src/socket_wrapper.cxx src/buffered_socket.cxx src/filtered_socket.cxx \
	src/serialize.cxx \
	src/cleanup_timer.c \
	src/buffered_io.c \
	src/fd-util.c src/fd_util.c \
	src/istream_gb.cxx \
	src/istream-ajp-body.c \
	src/stock.cxx src/pipe_stock.cxx \
	src/shutdown_listener.c \
	$(STOPWATCH_SRC) \
	src/tpool.c \
	src/direct.c 
test_run_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libnet.a \
	libputil.a \
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
	src/exec.cxx \
	src/istream_unlock.cxx \
	src/cache.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/tpool.c \
	src/clock.c \
	src/istream_rubber.cxx
test_run_http_cache_LDADD = $(AM_LDADD) \
	libraddress.a \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_memcached_client_SOURCES = test/run_memcached_client.cxx \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/buffered_io.c \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/stock.cxx src/pipe_stock.cxx \
	src/fd-util.c src/fd_util.c \
	src/cleanup_timer.c \
	src/shutdown_listener.c \
	src/direct.c
test_run_memcached_client_LDADD = $(LIBEVENT_LIBS) $(GLIB_LIBS) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_dump_memcached_choice_SOURCES = test/dump_memcached_choice.cxx \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/buffered_io.c \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/fd-util.c src/fd_util.c \
	src/cleanup_timer.c \
	src/direct.c \
	src/serialize.cxx \
	src/sink_buffer.cxx \
	src/tpool.c
test_dump_memcached_choice_LDADD = $(LIBEVENT_LIBS) $(GLIB_LIBS) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_cleanup_memcached_choice_SOURCES = test/cleanup_memcached_choice.cxx \
	src/address_list.cxx src/address_resolver.cxx \
	src/failure.cxx src/bulldog.c src/balancer.cxx \
	src/tcp_stock.cxx src/tcp_balancer.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/direct.c \
	src/cache.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/fd_util.c src/fd-util.c \
	src/exec.cxx \
	src/stock.cxx src/hstock.cxx \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/memcached_stock.cxx \
	src/http_cache_choice.cxx src/http_cache_rfc.cxx \
	src/http_util.cxx \
	src/serialize.cxx \
	src/sink_buffer.cxx \
	src/buffered_io.c \
	src/tpool.c \
	src/clock.c \
	$(STOPWATCH_SRC)
test_cleanup_memcached_choice_LDADD = $(AM_LDADD) \
	libraddress.a \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	libnet.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_fake_memcached_server_SOURCES = test/fake-memcached-server.c

test_t_memcached_client_SOURCES = test/t_memcached_client.cxx \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/socket_wrapper.cxx src/buffered_socket.cxx \
	src/cleanup_timer.c \
	src/direct.c \
	src/buffered_io.c \
	src/fd-util.c src/fd_util.c
test_t_memcached_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_delegate_SOURCES = test/run_delegate.cxx \
	src/delegate_stock.cxx src/delegate_client.cxx src/delegate_glue.cxx \
	src/exec.cxx \
	src/hstock.cxx src/stock.cxx \
	src/defer.cxx \
	src/fd_util.c
test_run_delegate_LDADD = $(AM_LDADD) \
	libputil.a \
	libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_dump_udp_SOURCES = test/dump_udp.cxx \
	src/udp_listener.cxx \
	src/fd_util.c
test_dump_udp_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libnet.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS)

test_dump_control_SOURCES = test/dump_control.cxx \
	src/udp_listener.cxx \
	src/fd_util.c \
	src/control_server.cxx
test_dump_control_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libnet.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS)

test_send_control_SOURCES = test/send-control.c
test_send_control_LDADD = $(LIBCM4ALL_SOCKET_LIBS)

test_run_ua_parser_SOURCES = test/run_ua_parser.c \
	src/ua_classification.c
test_run_ua_parser_LDADD = $(AM_LDADD) $(GLIB_LIBS)

test_run_parser_cdata_SOURCES = test/run_parser_cdata.cxx \
	src/xml_parser.cxx \
	src/buffered_io.c \
	src/cleanup_timer.c \
	src/fd_util.c
test_run_parser_cdata_LDADD = $(AM_LDADD) \
	libputil.a \
	libistream.a libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_css_parser_SOURCES = test/run_css_parser.cxx \
	src/css_parser.cxx \
	src/buffered_io.c \
	src/cleanup_timer.c \
	src/fd_util.c
test_run_css_parser_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libputil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(GLIB_LIBS)

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
	sparse $(SPARSE_FLAGS) $(src_cm4all_beng_proxy_SOURCES)

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

debug: src/cm4all-beng-proxy
	LD_LIBRARY_PATH=/usr/lib/debug:$(LD_LIBRARY_PATH) gdb -x gdbrun --args $< $(DEBUG_ARGS)

VALGRIND_FLAGS = --leak-check=yes --show-reachable=yes
VALGRIND_FLAGS += --track-fds=yes --track-origins=yes
VALGRIND_FLAGS += --suppressions=valgrind.suppressions

valgrind: export G_SLICE=always-malloc
valgrind: src/cm4all-beng-proxy
	valgrind $(VALGRIND_FLAGS) ./src/cm4all-beng-proxy $(DEBUG_ARGS)

perf: src/cm4all-beng-proxy
	perf record -f $< -D -u max -p 8080

# -DNO_DATE_HEADER -DNO_XATTR -DNO_LAST_MODIFIED_HEADER
benchmark: AM_CPPFLAGS += -DNDEBUG -DALWAYS_INLINE -DNO_ACCESS_LOG -DNO_XATTR
benchmark: src/cm4all-beng-proxy
	./src/cm4all-beng-proxy -D -u max -p 8080 -w 4

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
