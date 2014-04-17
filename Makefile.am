AUTOMAKE_OPTIONS = foreign 1.11 dist-bzip2 subdir-objects

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

#ifeq ($(ICC),1)
#CC = icc
#AM_CFLAGS = -std=gnu99 -x c -Wall -Werror -wd981
#endif


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
	src/util/Cast.hxx \
	src/util/ConstBuffer.hxx \
	src/product.h \
	src/capabilities.cxx src/capabilities.hxx \
	src/gerrno.h \
	src/gerror.h \
	src/abort-close.c src/abort-close.h \
	src/abort-flag.c src/abort-flag.h \
	src/abort-unref.c src/abort-unref.h \
	src/access-log.c src/access-log.h \
	src/address.c src/address.h \
	src/ajp-client.c src/ajp-client.h \
	src/ajp-headers.c src/ajp-headers.h \
	src/ajp-protocol.c src/ajp-protocol.h \
	src/ajp-read.h \
	src/ajp-request.c src/ajp-request.h \
	src/ajp-serialize.c src/ajp-serialize.h \
	src/ajp-util.h \
	src/args.c src/args.h \
	src/background.h \
	src/balancer.c src/balancer.h \
	src/bot.c src/bot.h \
	src/buffered_io.c src/buffered_io.h \
	src/bulldog.c src/bulldog.h \
	src/cache.c src/cache.h \
	src/cgi_glue.cxx src/cgi_glue.hxx \
	src/cgi_quark.h \
	src/cgi_parser.c src/cgi_parser.h \
	src/cgi_client.c src/cgi_client.h \
	src/cgi_launch.cxx src/cgi_launch.hxx \
	src/pivot_root.h \
	src/child_manager.c src/child_manager.h \
	src/child_socket.c src/child_socket.h \
	src/child_stock.c src/child_stock.h \
	src/client-socket.c src/client-socket.h \
	src/config.hxx \
	src/bp_connection.cxx src/connection.h \
	src/dchunk.h \
	src/defer.c src/defer.h \
	src/defer_event.h \
	src/delegate_client.c \
	src/delegate_client.h \
	src/delegate_glue.c src/delegate_glue.h \
	src/delegate_protocol.h \
	src/delegate_request.c src/delegate_request.h \
	src/delegate_stock.c src/delegate_stock.h \
	src/delegate_handler.cxx \
	src/dhashmap.h \
	src/direct.c src/direct.h \
	src/dpool.c src/dpool.h \
	src/drop.c src/drop.h \
	src/expansible-buffer.c src/expansible-buffer.h \
	src/expiry.h \
	src/failure.c src/failure.h \
	src/fcache.cxx src/fcache.h \
	src/fcgi_client.c src/fcgi_client.h \
	src/fcgi_launch.c src/fcgi_launch.h \
	src/fcgi_remote.c src/fcgi_remote.h \
	src/fcgi_protocol.h \
	src/fcgi_request.c src/fcgi_request.h \
	src/fcgi_serialize.c src/fcgi_serialize.h \
	src/fcgi_stock.c src/fcgi_stock.h \
	src/was_control.c src/was_control.h \
	src/was_output.c src/was_output.h \
	src/was_input.c src/was_input.h \
	src/was_client.c src/was_client.h \
	src/was_stock.c src/was_stock.h \
	src/was_launch.c \
	src/was_glue.c \
	src/fd-util.c src/fd-util.h \
	src/fd_util.c src/fd_util.h \
	src/file_headers.cxx src/file_headers.hxx \
	src/file_handler.cxx src/file_handler.hxx \
	src/fork.c src/fork.h \
	src/gb-io.c src/gb-io.h \
	src/get.cxx src/get.hxx \
	src/global.c src/global.h \
	src/growing-buffer.c src/growing-buffer.h \
	src/handler.cxx src/handler.hxx \
	src/file_not_found.cxx src/file_not_found.hxx \
	src/file_directory_index.cxx src/file_directory_index.hxx \
	src/hashmap.c src/hashmap.h \
	src/header-forward.c src/header-forward.h \
	src/header-parser.c src/header-parser.h \
	src/header-writer.c src/header-writer.h \
	src/header-copy.c src/header-copy.h \
	src/hostname.c src/hostname.h \
	src/http_body.c src/http_body.h \
	src/http_cache.cxx src/http_cache.h \
	src/http_cache_choice.cxx src/http_cache_choice.hxx \
	src/http_cache_internal.hxx \
	src/http_cache_document.cxx \
	src/http_cache_age.cxx src/http_cache_age.hxx \
	src/http_cache_heap.cxx src/http_cache_heap.hxx \
	src/http_cache_info.cxx \
	src/http_cache_memcached.cxx src/http_cache_memcached.hxx \
	src/http_cache_rfc.cxx \
	src/http_client.c src/http_client.h \
	src/http_error.c src/http_error.h \
	src/http_request.c src/http_request.h \
	src/http_server_internal.h \
	src/http_server.c src/http_server.h \
	src/http_server_send.c \
	src/http_server_request.c \
	src/http_server_read.c \
	src/http_server_response.c \
	src/http_string.c src/http_string.h \
	src/http_util.c src/http_util.h \
	src/http_quark.h \
	src/http_response.c src/http_response.h \
	src/control_server.c src/control_server.h \
	src/control_local.c src/control_local.h \
	src/bp_stats.cxx src/bp_stats.hxx \
	src/bp_control.cxx src/bp_control.hxx \
	src/listener.c src/listener.h \
	src/lock.h \
	src/memcached_client.cxx src/memcached_client.hxx \
	src/memcached_packet.hxx \
	src/memcached_protocol.hxx \
	src/memcached_stock.cxx src/memcached_stock.hxx \
	src/memcached_packet.cxx \
	src/xml_parser.c src/xml_parser.h \
	src/pevent.h \
	src/pipe-stock.c src/pipe-stock.h \
	src/pipe.c src/pipe.h \
	src/tpool.c \
	src/processor.cxx src/processor.h \
	src/penv.cxx src/penv.hxx \
	src/proxy_widget.cxx src/proxy_widget.hxx \
	src/random.c src/random.h \
	src/refcount.h \
	src/request_forward.cxx src/request_forward.hxx \
	src/request.cxx src/request.hxx \
	src/rerror.cxx \
	src/response.cxx \
	src/generate_response.cxx src/generate_response.hxx \
	src/resource_tag.cxx src/resource_tag.hxx \
	src/rewrite_uri.cxx src/rewrite_uri.hxx \
	src/rwlock.h \
	src/serialize.c src/serialize.h \
	src/shm.c src/shm.h \
	src/sink-gstring.h \
	src/sink-impl.h \
	src/socket_wrapper.c src/socket_wrapper.h \
	src/buffered_socket.c src/buffered_socket.h \
	src/filtered_socket.c src/filtered_socket.h \
	src/nop_socket_filter.c src/nop_socket_filter.h \
	src/static-file.c src/static-file.h \
	src/static-headers.c src/static-headers.h \
	src/stock.c src/stock.h \
	src/hstock.c src/hstock.h \
	src/mstock.cxx src/hstock.h \
	src/strmap.c src/strmap.h \
	src/strref-dpool.h \
	src/strref-pool.h \
	src/suffix_registry.cxx src/suffix_registry.hxx \
	src/tcache.cxx src/tcache.hxx \
	src/tcp-stock.c src/tcp-stock.h \
	src/tcp-balancer.c src/tcp-balancer.h \
	src/tpool.h \
	src/transformation.cxx src/transformation.hxx \
	src/tstock.cxx src/tstock.hxx \
	src/udp-listener.h \
	src/udp-distribute.h \
	src/uri-edit.h \
	src/uri-escape.h \
	src/uri-extract.h \
	src/uri-relative.h \
	src/widget.h \
	$(STOPWATCH_SRC) \
	src/clock.c src/clock.h \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/bp_cmdline.cxx \
	src/completion.h \
	src/crash.h \
	src/crash.c \
	src/bp_worker.cxx src/worker.h \
	src/dstring.c \
	src/istream_rubber.cxx src/istream_rubber.hxx \
	src/sink_rubber.cxx src/sink_rubber.hxx \
	src/session.c src/session.h \
	src/session_id.c src/session_id.h \
	src/session_manager.c src/session_manager.h \
	src/session_write.c src/session_write.h \
	src/session_read.c src/session_read.h \
	src/session_save.c src/session_save.h \
	src/cookie_jar.c src/cookie_jar.h \
	src/cookie_client.c src/cookie_client.h \
	src/cookie_server.c src/cookie_server.h \
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
	src/widget-ref.c \
	src/widget_session.cxx \
	src/widget_uri.cxx \
	src/widget-quark.h \
	src/widget_request.cxx src/widget_request.hxx \
	src/widget_registry.cxx src/widget_registry.hxx \
	src/widget_resolver.cxx src/widget_resolver.hxx \
	src/widget_approval.cxx src/widget_approval.hxx \
	src/widget-dump.h \
	src/widget-dump.c \
	src/escape_pool.c src/escape_pool.h \
	src/istream-escape.c src/istream-escape.h \
	src/istream-html-escape.c \
	src/pheaders.c src/pheaders.h \
	src/css_syntax.h \
	src/css_util.h \
	src/css_parser.c src/css_parser.h \
	src/css_processor.cxx src/css_processor.h \
	src/css_rewrite.cxx src/css_rewrite.hxx \
	src/text_processor.cxx src/text_processor.hxx \
	src/resource_loader.cxx src/resource_loader.hxx \
	src/widget_http.cxx src/widget_http.hxx \
	src/inline_widget.cxx src/inline_widget.hxx \
	src/frame.cxx src/frame.hxx \
	src/udp-listener.c \
	src/udp-distribute.c \
	src/istream-ajp-body.c \
	src/istream-gb.h \
	src/istream-gb.c \
	src/sigutil.h \
	src/address_quark.h \
	src/address_resolver.c src/address_resolver.h \
	src/address_string.h \
	src/address_string.c \
	src/uri-edit.c \
	src/uri-extract.c \
	src/uri-relative.c \
	src/uri_base.cxx src/uri_base.hxx \
	src/uri-escape.c \
	src/strset.c src/strset.h \
	src/dhashmap.c \
	src/istream-unlock.c \
	src/ua_classification.c src/ua_classification.h \
	src/log-client.c src/log-client.h \
	src/log-launch.c src/log-launch.h \
	src/log-glue.c src/log-glue.h \
	src/notify.c src/notify.h \
	src/thread_job.h \
	src/thread_queue.c src/thread_queue.h \
	src/thread_worker.c src/thread_worker.h \
	src/thread_pool.c src/thread_pool.h \
	src/ssl_init.cxx src/ssl_init.hxx \
	src/ssl_client.cxx src/ssl_client.h \
	src/ssl_factory.cxx src/ssl_factory.hxx \
	src/ssl_filter.cxx src/ssl_filter.hxx \
	src/thread_socket_filter.cxx src/thread_socket_filter.hxx \
	src/bp_instance.cxx src/bp_instance.hxx \
	src/shutdown_listener.c src/shutdown_listener.h \
	src/bp_main.cxx
src_cm4all_beng_proxy_LDADD = $(AM_LDADD) \
	-lcap \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
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
	src/nfs_handler.c src/nfs_handler.h \
	src/istream_nfs.cxx src/istream_nfs.hxx \
	src/nfs_stock.c src/nfs_stock.h \
	src/nfs_client.c src/nfs_client.h \
	src/nfs_cache.cxx src/nfs_cache.h \
	src/nfs_request.c src/nfs_request.h
endif

src_cm4all_beng_lb_SOURCES = \
	src/isolate.cxx src/isolate.hxx \
	src/abort-close.c src/abort-close.h \
	src/capabilities.cxx src/capabilities.hxx \
	src/clock.c src/clock.h \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/tpool.c \
	src/stock.c \
	src/hstock.c \
	src/tcp-stock.c \
	src/tcp-balancer.c \
	src/pipe-stock.c \
	src/failure.c \
	src/balancer.c \
	src/address.c \
	src/address_list.c \
	src/address_resolver.c src/address_resolver.h \
	src/address_string.h \
	src/address_string.c \
	src/address_edit.h \
	src/address_edit.c \
	src/address_sticky.c src/address_sticky.h \
	src/listener.c \
	src/client-socket.c \
	src/client-balancer.c src/client-balancer.h \
	src/bulldog.c \
	src/child_manager.c src/child_manager.h \
	src/hashmap.c \
	src/strmap.c \
	src/cache.c \
	src/http_body.c \
	src/http_server.c \
	src/http_server_send.c \
	src/http_server_request.c \
	src/http_server_read.c \
	src/http_server_response.c \
	src/http_client.c \
	src/http_string.c \
	src/http_util.c src/http_util.h \
	src/header-writer.c \
	src/header-parser.c \
	src/uri_base.cxx src/uri_base.hxx \
	src/regex.c src/regex.h \
	src/http_address.c \
	src/uri-relative.c \
	src/uri-extract.c \
	src/uri-edit.c \
	src/uri-escape.c \
	src/fifo-buffer.c \
	src/growing-buffer.c \
	src/istream-gb.c \
	src/buffered_io.c \
	src/fd_util.c \
	src/fd-util.c \
	$(STOPWATCH_SRC) \
	src/udp-listener.c src/udp-listener.h \
	src/control_server.c src/control_server.h \
	src/direct.c \
	src/access-log.c \
	src/cookie_server.c \
	src/log-glue.c \
	src/log-client.c \
	src/log-launch.c \
	src/sticky.h \
	src/notify.h \
	src/notify.c \
	src/thread_job.h \
	src/thread_queue.c src/thread_queue.h \
	src/thread_worker.c src/thread_worker.h \
	src/thread_pool.c src/thread_pool.h \
	src/ssl_quark.hxx \
	src/ssl_init.hxx \
	src/ssl_init.cxx \
	src/ssl_config.hxx \
	src/ssl_factory.hxx \
	src/ssl_factory.cxx \
	src/ssl_filter.hxx \
	src/ssl_filter.cxx \
	src/socket_wrapper.c src/buffered_socket.c src/filtered_socket.c \
	src/thread_socket_filter.cxx src/thread_socket_filter.hxx \
	src/nop_thread_socket_filter.cxx src/nop_thread_socket_filter.hxx \
	src/ping.c src/ping.h \
	src/shutdown_listener.c src/shutdown_listener.h \
	src/lb_global.cxx \
	src/lb_cmdline.hxx \
	src/lb_cmdline.cxx \
	src/lb_stats.c src/lb_stats.hxx \
	src/lb_control.hxx \
	src/lb_control.cxx \
	src/lb_listener.hxx \
	src/lb_listener.cxx \
	src/lb_connection.hxx \
	src/lb_connection.cxx \
	src/lb_log.h src/lb_log.cxx \
	src/lb_http.hxx \
	src/lb_http.cxx \
	src/lb_jvm_route.c src/lb_jvm_route.hxx \
	src/lb_headers.c src/lb_headers.hxx \
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
	libraddress.a \
	libistream.a

libutil_a_SOURCES = \
	src/cast.hxx \
	src/djbhash.c src/djbhash.h \
	src/format.c format.h \
	src/date.c src/date.h \
	src/gmtime.c src/gmtime.h \
	src/html-chars.h \
	src/async.h \
	src/lease.h \
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

libmemory_a_SOURCES = \
	src/mmap.h \
	src/fb_pool.c src/fb_pool.h \
	src/slice.c src/slice.h \
	src/rubber.cxx src/rubber.hxx

libpool_a_SOURCES = \
	src/pstring.c \
	src/pool.c src/pool.h

libraddress_a_SOURCES = \
	src/regex.c src/regex.h \
	src/param_array.hxx src/param_array.cxx \
	src/namespace_options.c src/namespace_options.h \
	src/mount_list.c src/mount_list.h \
	src/bind_mount.c src/bind_mount.h \
	src/jail.c src/jail.h \
	src/child_options.h \
	src/rlimit_options.c src/rlimit_options.h \
	src/cgi_address.cxx src/cgi_address.hxx \
	src/file_address.c src/file_address.h \
	src/nfs_address.c src/nfs_address.h \
	src/lhttp_address.cxx src/lhttp_address.hxx \
	src/http_address.c src/http_address.h \
	src/address_list.h \
	src/address_list.c \
	src/resource_address.cxx src/resource_address.hxx

libistream_a_SOURCES = \
	src/istream_pause.c src/istream_pause.h \
	src/istream-buffer.h \
	src/istream-impl.h \
	src/istream-internal.h \
	src/istream-invoke.h \
	src/istream-new.h \
	src/istream-direct.h \
	src/istream.h \
	src/istream-forward.c src/istream-forward.h \
	src/istream-memory.c \
	src/istream-null.c \
	src/istream-zero.c \
	src/istream-block.c \
	src/istream-string.c \
	src/istream_file.c src/istream_file.h \
	src/istream-chunked.c \
	src/istream-dechunk.c \
	src/istream_fcgi.c \
	src/istream-cat.c \
	src/istream-pipe.c \
	src/istream-delayed.c \
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
	src/istream-replace.c \
	src/istream-socket.c src/istream-socket.h \
	src/istream-socketpair.c \
	src/istream-notify.h \
	src/istream-notify.c \
	src/fifo-buffer.c src/fifo-buffer.h \
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
	test/run-ping \
	test/run_was \
	test/was_mirror \
	test/benchmark-gmtime test/format-http-date \
	test/request_translation \
	test/run-subst \
	test/run-cookie-client \
	test/run-cookie-server \
	test/run-header-parser \
	test/run-html-unescape test/run-html-escape \
	test/run_processor \
	test/run-client-balancer \
	test/run_client \
	test/run_memcached_client \
	test/run_http_cache \
	test/dump_memcached_choice \
	test/cleanup_memcached_choice \
	test/fake-memcached-server \
	test/run_delegate \
	test/dump-udp test/dump-control test/send-control \
	test/run_ua_parser \
	test/run-parser-cdata

if HAVE_LIBNFS
noinst_PROGRAMS += test/run_nfs_client
endif

FILTER_TESTS = test/t-istream-cat test/t-istream-chunked test/t-istream-dechunk \
	test/t-istream-pipe test/t-istream-hold test/t-istream-delayed \
	test/t-istream-subst test/t-istream-deflate test/t-istream-byte \
	test/t-istream-iconv test/t-istream-replace test/t-istream-replace2 \
	test/t-istream-html-escape test/t-istream-fcgi \
	test/t_sink_header \
	test/t_sink_header_empty \
	test/t_istream_processor

check_PROGRAMS = \
	test/t-hashmap test/t-cache test/t_tcache \
	test/t-header-forward \
	test/t-stock \
	test/t_resource_address \
	test/t_http_address \
	test/t-uri-verify \
	test/t-uri-relative \
	test/t-html-escape \
	test/t-escape-css \
	test/t-expansible-buffer \
	$(FILTER_TESTS) \
	test/t-istream-tee \
	test/t-growing-buffer \
	test/t_balancer \
	test/t_cgi \
	test/run_http_server \
	test/t-http-server \
	test/t-http-client test/t-http-util \
	test/t_http_cache \
	test/t_ajp_client \
	test/t_fcgi_client \
	test/t-cookie-client \
	test/t-cookie-server \
	test/t_pool \
	test/t-shm test/t-dpool test/t-session \
	test/t_rubber test/t_sink_rubber \
	test/t_slice \
	test/t_rewrite_uri \
	test/t_widget_registry \
	test/t_widget_resolver \
	test/t_wembed test/t_widget_http \
	test/t_processor \
	test/t_memcached_client

TESTS = test/t-hashmap test/t-cache test/t_tcache \
	test/t-header-forward \
	test/t-stock \
	test/t_resource_address \
	test/t_http_address \
	test/t-uri-verify \
	test/t-uri-relative \
	test/t-html-escape \
	test/t-escape-css \
	test/t-expansible-buffer \
	$(FILTER_TESTS) \
	test/t-istream-tee \
	test/t-growing-buffer \
	test/t_balancer \
	test/t_cgi \
	test/t-http-server.py test/t-http-server \
	test/t-http-client test/t-http-util \
	test/t_http_cache \
	test/t_ajp_client \
	test/t_fcgi_client \
	test/t_processor \
	test/t_memcached_client \
	test/t-cookie-client.py test/t-cookie-client \
	test/t-cookie-server \
	test/t-shm test/t-dpool test/t-session \
	test/t_pool \
	test/t_rubber test/t_sink_rubber \
	test/t_slice \
	test/t_rewrite_uri \
	test/t_widget_registry \
	test/t_widget_resolver \
	test/t_wembed test/t_widget_http

test_run_ping_SOURCES = test/run-ping.c \
	src/ping.c \
	src/address_string.c
test_run_ping_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

test_run_was_SOURCES = test/run_was.c \
	src/was_launch.c src/was_client.c src/was_control.c src/was_input.c src/was_output.c \
	src/strmap.c src/hashmap.c \
	src/fifo-buffer.c src/buffered_io.c \
	src/cleanup_timer.c \
	src/direct.c \
	src/fd_util.c src/fd-util.c
test_run_was_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

test_was_mirror_SOURCES = test/was_mirror.c \
	src/was_server.c src/was_control.c src/was_input.c src/was_output.c \
	src/fifo-buffer.c \
	src/buffered_io.c \
	src/strmap.c src/hashmap.c \
	src/direct.c \
	src/fd_util.c src/fd-util.c
test_was_mirror_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

test_t_hashmap_SOURCES = test/t-hashmap.c src/hashmap.c
test_t_hashmap_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_cache_SOURCES = test/t-cache.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/clock.c \
	src/cache.c src/hashmap.c
test_t_cache_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_t_tcache_SOURCES = test/t_tcache.cxx src/tcache.cxx src/cache.c \
	src/translate_response.cxx \
	src/hashmap.c src/strmap.c \
	src/widget_view.cxx \
	src/strset.c \
	src/uri-relative.c src/uri-edit.c src/uri-escape.c \
	src/uri-extract.c src/uri_base.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/clock.c \
	src/tpool.c \
	src/transformation.cxx
test_t_tcache_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) -lrt

test_t_header_forward_SOURCES = test/t-header-forward.c \
	src/header-forward.c src/header-writer.c \
	src/header-copy.c \
	src/http_string.c \
	src/hashmap.c src/strmap.c \
	src/growing-buffer.c \
	src/cookie_client.c src/cookie_jar.c \
	src/shm.c src/dpool.c src/dstring.c src/dhashmap.c \
	src/random.c src/fd-util.c \
	src/fd_util.c \
	src/crash.c \
	src/session_id.c \
	src/clock.c \
	src/tpool.c
test_t_header_forward_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS)

test_t_stock_SOURCES = test/t-stock.c src/stock.c
test_t_stock_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) $(GLIB_LIBS)

test_t_resource_address_SOURCES = test/t_resource_address.cxx \
	src/regex.c \
	src/http_address.c src/uri-relative.c \
	src/uri-escape.c src/uri-edit.c \
	src/uri-extract.c src/uri_base.cxx \
	src/address_list.c
test_t_resource_address_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(GLIB_LIBS) $(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS)

test_t_http_address_SOURCES = test/t_http_address.c \
	src/http_address.c src/uri-relative.c \
	src/uri-escape.c src/uri-edit.c \
	src/uri-extract.c src/uri_base.cxx \
	src/regex.c \
	src/address_list.c
test_t_http_address_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(GLIB_LIBS) $(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS)

test_t_uri_verify_SOURCES = test/t-uri-verify.c
test_t_uri_verify_LDADD = $(AM_LDADD) libutil.a

test_t_uri_relative_SOURCES = test/t-uri-relative.c \
	src/uri-relative.c \
	src/uri-extract.c
test_t_uri_relative_LDADD = $(AM_LDADD) libpool.a libmemory.a $(LIBCM4ALL_DAEMON_LIBS)

test_t_expansible_buffer_SOURCES = test/t-expansible-buffer.c \
	src/expansible-buffer.c
test_t_expansible_buffer_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS)

T_ISTREAM_FILTER_SRC = \
	src/direct.c \
	src/buffered_io.c \
	src/pipe-stock.c src/stock.c \
	src/fd_util.c \
	src/growing-buffer.c src/fd-util.c
T_ISTREAM_FILTER_LIB = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) $(LIBZ_LIBS)

test_t_istream_cat_SOURCES = test/t-istream-cat.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_cat_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_chunked_SOURCES = test/t-istream-chunked.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_chunked_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_dechunk_SOURCES = test/t-istream-dechunk.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_dechunk_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_pipe_SOURCES = test/t-istream-pipe.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_pipe_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_hold_SOURCES = test/t-istream-hold.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_hold_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_delayed_SOURCES = test/t-istream-delayed.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_delayed_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_subst_SOURCES = test/t-istream-subst.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_subst_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_deflate_SOURCES = test/t-istream-deflate.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_deflate_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_byte_SOURCES = test/t-istream-byte.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_byte_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_iconv_SOURCES = test/t-istream-iconv.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_iconv_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_replace_SOURCES = test/t-istream-replace.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_replace_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_replace2_SOURCES = test/t-istream-replace2.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_replace2_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_html_escape_SOURCES = \
	src/istream-escape.c \
	src/istream-html-escape.c \
	test/t-istream-html-escape.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_html_escape_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_fcgi_SOURCES = test/t-istream-cat.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_fcgi_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_processor_SOURCES = test/t_istream_processor.cxx \
	src/istream-escape.c \
	src/istream-html-escape.c \
	src/uri-relative.c src/uri-escape.c src/uri-edit.c \
	src/uri_base.cxx \
	src/random.c \
	src/crash.c \
	src/session.c src/session_manager.c \
	src/cookie_client.c src/cookie_jar.c \
	src/http_string.c \
	src/strmap.c src/hashmap.c \
	src/strset.c \
	src/penv.cxx src/processor.cxx \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.c src/css_rewrite.cxx \
	src/widget_request.cxx src/widget_approval.cxx \
	src/widget.cxx src/widget_init.cxx src/widget-ref.c src/widget_uri.cxx src/args.c \
	src/widget_session.cxx src/xml_parser.c src/widget_class.cxx \
	src/widget_view.cxx \
	src/tpool.c src/rewrite_uri.cxx src/widget_resolver.cxx \
	src/uri-extract.c \
	src/dhashmap.c src/dpool.c src/shm.c src/dstring.c \
	src/global.c src/transformation.cxx \
	src/expansible-buffer.c \
	src/clock.c \
	$(T_ISTREAM_FILTER_SRC)
test_t_istream_processor_LDADD = $(AM_LDADD) \
	libraddress.a \
	libutil.a \
	$(T_ISTREAM_FILTER_LIB) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_sink_header_SOURCES = test/t_sink_header.c \
	$(T_ISTREAM_FILTER_SRC)
test_t_sink_header_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_sink_header_empty_SOURCES = test/t_sink_header_empty.c \
	$(T_ISTREAM_FILTER_SRC)
test_t_sink_header_empty_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_tee_SOURCES = test/t_istream_tee.c \
	src/sink-gstring.c \
	src/sink-close.c \
	$(T_ISTREAM_FILTER_SRC)
test_t_istream_tee_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_growing_buffer_SOURCES = test/t-growing-buffer.c \
	src/istream-gb.c \
	$(T_ISTREAM_FILTER_SRC)
test_t_growing_buffer_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_balancer_SOURCES = test/t_balancer.cxx \
	test/PoolTest.hxx \
	src/bulldog.c \
	src/balancer.c src/cache.c src/failure.c \
	src/hashmap.c \
	src/address_list.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/clock.c
test_t_balancer_CPPFLAGS = $(AM_CPPFLAGS)$ $(CPPUNIT_CFLAGS)
test_t_balancer_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(CPPUNIT_LIBS)

test_t_cgi_SOURCES = test/t_cgi.cxx \
	$(STOPWATCH_SRC) \
	src/clock.c \
	src/cgi_glue.cxx src/fork.c \
	src/cgi_client.c src/cgi_launch.cxx \
	src/cgi_parser.c \
	src/uri_base.cxx src/uri-escape.c src/uri-relative.c \
	src/uri-extract.c \
	src/cleanup_timer.c \
	src/regex.c \
	src/tpool.c \
	src/fifo-buffer.c src/buffered_io.c src/growing-buffer.c \
	src/header-parser.c \
	src/strmap.c src/hashmap.c \
	src/fd_util.c src/fd-util.c \
	src/child_manager.c src/child_manager.h \
	src/crash.c \
	src/abort-flag.c src/direct.c
test_t_cgi_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS)

test_t_http_server_SOURCES = test/t-http-server.c \
	src/http_util.c \
	src/http_server.c src/http_server_send.c src/http_server_request.c \
	src/http_server_read.c src/http_server_response.c \
	src/socket_wrapper.c src/buffered_socket.c src/filtered_socket.c \
	src/cleanup_timer.c \
	src/address.c \
	src/fifo-buffer.c \
	src/buffered_io.c \
	src/strmap.c src/hashmap.c \
	src/header-writer.c \
	src/istream-gb.c \
	src/pipe-stock.c src/stock.c \
	src/direct.c \
	src/http_body.c \
	src/fd_util.c src/fd-util.c \
	src/growing-buffer.c \
	src/header-parser.c \
	src/tpool.c
test_t_http_server_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_http_server_SOURCES = test/run_http_server.c \
	src/http_util.c \
	src/http_server.c src/http_server_send.c src/http_server_request.c \
	src/http_server_read.c src/http_server_response.c \
	src/socket_wrapper.c src/buffered_socket.c src/filtered_socket.c \
	src/address.c \
	src/fifo-buffer.c \
	src/duplex.c \
	src/buffered_io.c \
	src/strmap.c src/hashmap.c \
	src/header-writer.c \
	src/istream-gb.c \
	src/pipe-stock.c src/stock.c \
	src/direct.c \
	src/http_body.c \
	src/fd_util.c src/fd-util.c \
	src/cleanup_timer.c \
	src/shutdown_listener.c \
	src/growing-buffer.c \
	src/header-parser.c \
	src/tpool.c src/event2.c
test_run_http_server_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBEVENT_LIBS) $(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_http_client_SOURCES = test/t-http-client.c \
	src/t_client.h \
	src/http_client.c \
	src/socket_wrapper.c src/buffered_socket.c src/filtered_socket.c \
	src/cleanup_timer.c \
	$(STOPWATCH_SRC) \
	src/strmap.c src/hashmap.c \
	src/growing-buffer.c src/fifo-buffer.c \
	src/header-writer.c \
	src/istream-gb.c \
	src/direct.c \
	src/http_body.c src/header-parser.c \
	src/buffered_io.c src/fd_util.c src/fd-util.c \
	src/tpool.c
test_t_http_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_http_util_SOURCES = test/t-http-util.c \
	src/http_util.c
test_t_http_util_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_http_cache_SOURCES = test/t_http_cache.cxx \
	src/tpool.c \
	src/sink_rubber.cxx \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/strmap.c src/hashmap.c \
	src/fifo-buffer.c src/header-parser.c \
	src/growing-buffer.c \
	src/istream_rubber.cxx \
	src/istream-gb.c \
	src/http_cache.cxx src/http_cache_rfc.cxx \
	src/http_cache_info.cxx src/http_cache_document.cxx \
	src/http_cache_heap.cxx src/http_cache_age.cxx \
	src/uri-edit.c src/uri-relative.c src/uri-escape.c \
	src/uri-extract.c src/uri_base.cxx \
	src/abort-unref.c src/cache.c \
	src/header-writer.c src/http_util.c \
	src/clock.c \
	src/istream-unlock.c
test_t_http_cache_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(GLIB_LIBS)

test_t_cookie_client_SOURCES = test/t-cookie-client.c \
	src/tpool.c \
	src/clock.c \
	src/cookie_client.c src/cookie_jar.c \
	src/http_string.c \
	src/header-writer.c src/growing-buffer.c \
	src/strmap.c src/hashmap.c \
	src/shm.c src/dpool.c src/dstring.c
test_t_cookie_client_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_cookie_server_SOURCES = test/t-cookie-server.c \
	src/cookie_server.c \
	src/http_string.c \
	src/strmap.c src/hashmap.c
test_t_cookie_server_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_t_shm_SOURCES = test/t-shm.c src/shm.c
test_t_shm_LDADD = $(AM_LDADD) $(LIBCM4ALL_DAEMON_LIBS)

test_t_dpool_SOURCES = test/t-dpool.c src/shm.c src/dpool.c
test_t_dpool_LDADD = $(AM_LDADD) $(LIBCM4ALL_DAEMON_LIBS)

test_t_session_SOURCES = test/t-session.c src/shm.c \
	src/dpool.c src/dstring.c src/dhashmap.c \
	src/clock.c \
	src/random.c src/fd-util.c src/fd_util.c \
	src/crash.c \
	src/session.c src/session_manager.c
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
	src/strset.c \
	src/escape_pool.c \
	src/istream-escape.c \
	src/istream-html-escape.c \
	src/rewrite_uri.cxx src/widget_uri.cxx src/widget.cxx \
	src/widget_init.cxx src/widget_view.cxx src/transformation.cxx \
	src/sink-gstring.c \
	src/args.c \
	src/strmap.c src/hashmap.c \
	src/uri-relative.c src/uri-edit.c src/uri-escape.c \
	src/uri-extract.c src/uri_base.cxx
test_t_rewrite_uri_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_widget_registry_SOURCES = test/t_widget_registry.cxx \
	src/widget_registry.cxx src/stock.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/transformation.cxx \
	src/tcache.cxx src/cache.c \
	src/translate_response.cxx \
	src/widget_view.cxx \
	src/hashmap.c src/strmap.c src/abort-unref.c \
	src/strset.c \
	src/uri-relative.c src/uri-escape.c src/uri-edit.c \
	src/uri-extract.c src/uri_base.cxx \
	src/tpool.c \
	src/clock.c
test_t_widget_registry_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_widget_resolver_SOURCES = test/t_widget_resolver.cxx \
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
	src/fifo-buffer.c \
	src/hashmap.c src/strmap.c src/strset.c \
	src/http_util.c \
	src/uri-escape.c \
	src/global.c
test_t_wembed_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(GLIB_LIBS)

test_t_widget_http_SOURCES = test/t_widget_http.cxx \
	src/widget_http.cxx src/widget_uri.cxx src/widget_request.cxx \
	src/pheaders.c \
	src/crash.c \
	src/widget_session.cxx src/session.c src/session_manager.c src/session_id.c \
	src/widget_view.cxx src/widget.cxx src/widget_init.cxx \
	src/random.c src/fd-util.c \
	src/fd_util.c \
	src/args.c src/uri-escape.c \
	src/uri-relative.c src/uri-edit.c src/uri-extract.c src/uri_base.cxx \
	src/resource_tag.cxx \
	src/cookie_client.c src/cookie_jar.c \
	src/header-parser.c src/header-forward.c src/header-writer.c \
	src/header-copy.c \
	src/strmap.c src/hashmap.c \
	src/strset.c \
	src/growing-buffer.c src/fifo-buffer.c \
	src/http_string.c src/http_util.c \
	src/istream-escape.c \
	src/transformation.cxx \
	src/dpool.c src/dstring.c src/dhashmap.c src/shm.c \
	src/clock.c \
	src/tpool.c
test_t_widget_http_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
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
	src/socket_wrapper.c src/buffered_socket.c \
	src/fifo-buffer.c src/buffered_io.c src/direct.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/translate_client.cxx src/tstock.cxx src/growing-buffer.c src/gb-io.c \
	src/translate_response.cxx \
	src/failure.c src/bulldog.c src/balancer.c src/cache.c \
	src/tcp-stock.c src/client-socket.c \
	src/fd_util.c src/fd-util.c \
	src/stock.c src/hstock.c src/hashmap.c src/strmap.c \
	src/strset.c \
	src/abort-unref.c \
	src/uri-edit.c src/uri_base.cxx \
	src/uri-escape.c src/uri-relative.c \
	src/uri-extract.c \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/clock.c \
	$(STOPWATCH_SRC)
test_request_translation_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_subst_SOURCES = test/run-subst.c \
	src/fifo-buffer.c src/buffered_io.c \
	src/cleanup_timer.c \
	src/fd_util.c
test_run_subst_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_cookie_client_SOURCES = test/run-cookie-client.c \
	src/cookie_client.c src/cookie_jar.c src/http_string.c \
	src/header-writer.c src/growing-buffer.c \
	src/tpool.c src/strmap.c src/hashmap.c \
	src/shm.c src/dpool.c src/dstring.c \
	src/clock.c
test_run_cookie_client_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_cookie_server_SOURCES = test/run-cookie-server.c \
	src/cookie_server.c src/http_string.c \
	src/strmap.c src/hashmap.c
test_run_cookie_server_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_header_parser_SOURCES = test/run-header-parser.c \
	src/header-parser.c src/growing-buffer.c src/fifo-buffer.c \
	src/tpool.c src/strmap.c \
	src/hashmap.c
test_run_header_parser_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
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
	src/processor.cxx src/penv.cxx src/xml_parser.c \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.c src/css_rewrite.cxx \
	src/widget.cxx src/widget_init.cxx src/widget_approval.cxx src/widget-ref.c \
	src/uri_base.cxx src/uri-relative.c src/uri-edit.c src/uri-extract.c \
	src/uri-escape.c \
	src/strmap.c src/hashmap.c \
	src/strset.c \
	src/growing-buffer.c src/fifo-buffer.c \
	src/tpool.c \
	src/istream-html-escape.c src/istream-escape.c \
	src/header-writer.c src/args.c src/buffered_io.c \
	src/tcp-stock.c src/stock.c src/hstock.c \
	src/client-socket.c src/fd_util.c \
	src/header-parser.c \
	src/widget_request.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/failure.c src/bulldog.c \
	src/balancer.c src/http_address.c src/cache.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/shm.c src/dpool.c src/dstring.c src/dhashmap.c \
	src/expansible-buffer.c \
	src/clock.c \
	$(STOPWATCH_SRC)
test_run_processor_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_processor_SOURCES = test/t_processor.cxx \
	src/processor.cxx src/penv.cxx src/xml_parser.c \
	src/text_processor.cxx \
	src/css_processor.cxx src/css_parser.c src/css_rewrite.cxx \
	src/widget.cxx src/widget_init.cxx src/widget_approval.cxx src/widget-ref.c \
	src/uri-relative.c src/uri-edit.c src/uri_base.cxx \
	src/uri-escape.c \
	src/strmap.c src/hashmap.c \
	src/strset.c \
	src/growing-buffer.c src/fifo-buffer.c \
	src/tpool.c \
	src/istream-html-escape.c src/istream-escape.c \
	src/header-writer.c src/args.c src/buffered_io.c \
	src/tcp-stock.c src/stock.c src/hstock.c \
	src/client-socket.c src/fd_util.c \
	src/header-parser.c \
	src/widget_request.cxx \
	src/widget_view.cxx \
	src/transformation.cxx \
	src/failure.c src/bulldog.c \
	src/balancer.c src/cache.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/uri-extract.c \
	src/shm.c src/dpool.c src/dstring.c src/dhashmap.c \
	src/expansible-buffer.c \
	src/clock.c \
	$(STOPWATCH_SRC)
test_t_processor_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_client_balancer_SOURCES = test/run-client-balancer.c \
	src/address_list.c \
	src/client-balancer.c \
	src/client-socket.c \
	src/balancer.c \
	src/failure.c \
	src/bulldog.c \
	src/cache.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/hashmap.c \
	src/fd_util.c \
	src/clock.c \
	$(STOPWATCH_SRC)
test_run_client_balancer_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBEVENT_LIBS) \
	-lrt \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(GLIB_LIBS)

if HAVE_LIBNFS

test_run_nfs_client_SOURCES = test/run_nfs_client.cxx \
	src/nfs_client.c src/istream_nfs.cxx \
	src/hashmap.c src/strmap.c \
	src/sink_fd.c \
	src/direct.c \
	src/fd_util.c src/fd-util.c \
	src/stock.c src/pipe-stock.c \
	src/static-headers.c \
	src/shutdown_listener.c
test_run_nfs_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBNFS_LIBS) \
	$(GLIB_LIBS)
endif

test_t_fcgi_client_SOURCES = test/t_fcgi_client.c \
	src/fcgi_client.c src/fcgi_serialize.c \
	src/socket_wrapper.c src/buffered_socket.c \
	src/buffered_io.c src/fifo-buffer.c \
	src/cleanup_timer.c \
	src/strmap.c src/hashmap.c \
	src/header-parser.c \
	src/growing-buffer.c src/istream-gb.c \
	src/fd-util.c src/fd_util.c \
	src/tpool.c \
	src/direct.c
test_t_fcgi_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_t_ajp_client_SOURCES = test/t_ajp_client.c \
	src/t_client.h src/tio.h \
	src/ajp-client.c src/ajp-serialize.c \
	src/ajp-headers.c src/ajp-protocol.c \
	src/socket_wrapper.c src/buffered_socket.c \
	src/cleanup_timer.c \
	src/serialize.c \
	src/buffered_io.c src/fifo-buffer.c \
	src/growing-buffer.c src/fd-util.c src/fd_util.c \
	src/strmap.c src/hashmap.c \
	src/istream-gb.c \
	src/istream-ajp-body.c \
	src/direct.c 
test_t_ajp_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) \
	$(GLIB_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_run_client_SOURCES = test/run_client.cxx \
	src/ssl_init.cxx \
	src/ssl_client.cxx \
	src/ssl_factory.cxx \
	src/ssl_filter.cxx \
	src/thread_socket_filter.cxx \
	src/thread_pool.c src/thread_queue.c src/thread_worker.c \
	src/notify.c \
	src/client-socket.c \
	src/http_client.c src/http_body.c \
	src/header-writer.c src/header-parser.c \
	src/ajp-client.c src/ajp-serialize.c \
	src/ajp-headers.c src/ajp-protocol.c \
	src/socket_wrapper.c src/buffered_socket.c src/filtered_socket.c \
	src/serialize.c \
	src/cleanup_timer.c \
	src/buffered_io.c src/fifo-buffer.c \
	src/growing-buffer.c src/fd-util.c src/fd_util.c \
	src/strmap.c src/hashmap.c \
	src/istream-gb.c \
	src/istream-ajp-body.c \
	src/stock.c src/pipe-stock.c \
	src/shutdown_listener.c \
	$(STOPWATCH_SRC) \
	src/tpool.c \
	src/direct.c 
test_run_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
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
	src/http_util.c \
	src/http_address.c src/uri-edit.c \
	src/uri_base.cxx src/uri-relative.c src/uri-extract.c \
	src/uri-escape.c \
	src/regex.c \
	src/address_list.c \
	src/istream-unlock.c \
	src/growing-buffer.c \
	src/strmap.c src/hashmap.c \
	src/cache.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/tpool.c \
	src/clock.c \
	src/istream_rubber.cxx
test_run_http_cache_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_HTTP_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_memcached_client_SOURCES = test/run_memcached_client.cxx \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/buffered_io.c src/fifo-buffer.c \
	src/socket_wrapper.c src/buffered_socket.c \
	src/stock.c src/pipe-stock.c \
	src/fd-util.c src/fd_util.c \
	src/cleanup_timer.c \
	src/shutdown_listener.c \
	src/direct.c
test_run_memcached_client_LDADD = $(LIBEVENT_LIBS) $(GLIB_LIBS) \
	libistream.a libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_dump_memcached_choice_SOURCES = test/dump_memcached_choice.cxx \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/buffered_io.c src/fifo-buffer.c \
	src/socket_wrapper.c src/buffered_socket.c \
	src/fd-util.c src/fd_util.c \
	src/cleanup_timer.c \
	src/direct.c \
	src/serialize.c \
	src/growing-buffer.c src/sink_buffer.cxx \
	src/strmap.c src/hashmap.c \
	src/tpool.c
test_dump_memcached_choice_LDADD = $(LIBEVENT_LIBS) $(GLIB_LIBS) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_cleanup_memcached_choice_SOURCES = test/cleanup_memcached_choice.cxx \
	src/address_list.c src/address_resolver.c \
	src/failure.c src/bulldog.c src/balancer.c \
	src/tcp-stock.c src/tcp-balancer.c \
	src/socket_wrapper.c src/buffered_socket.c \
	src/direct.c \
	src/cache.c \
	src/cleanup_timer.c src/cleanup_timer.h \
	src/client-socket.c src/fd_util.c src/fd-util.c \
	src/stock.c src/hstock.c \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/memcached_stock.cxx \
	src/http_cache_choice.cxx src/http_cache_rfc.cxx \
	src/http_util.c \
	src/serialize.c \
	src/sink_buffer.cxx \
	src/growing-buffer.c \
	src/buffered_io.c src/fifo-buffer.c \
	src/hashmap.c src/strmap.c \
	src/tpool.c \
	src/clock.c \
	$(STOPWATCH_SRC)
test_cleanup_memcached_choice_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_SOCKET_LIBS)

test_fake_memcached_server_SOURCES = test/fake-memcached-server.c

test_t_memcached_client_SOURCES = test/t_memcached_client.cxx \
	src/memcached_client.cxx src/memcached_packet.cxx \
	src/socket_wrapper.c src/buffered_socket.c \
	src/cleanup_timer.c \
	src/direct.c \
	src/fifo-buffer.c src/buffered_io.c \
	src/fd-util.c src/fd_util.c
test_t_memcached_client_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	$(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_delegate_SOURCES = test/run_delegate.c \
	src/delegate_stock.c src/delegate_client.c src/delegate_glue.c \
	src/hashmap.c src/hstock.c src/stock.c \
	src/defer.c \
	src/fd_util.c
test_run_delegate_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	libraddress.a \
	libutil.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_dump_udp_SOURCES = test/dump-udp.c \
	src/udp-listener.c \
	src/address_string.c \
	src/fd_util.c
test_dump_udp_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS)

test_dump_control_SOURCES = test/dump-control.c \
	src/udp-listener.c \
	src/address_string.c \
	src/fd_util.c \
	src/control_server.c
test_dump_control_LDADD = $(AM_LDADD) \
	libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS) $(LIBCM4ALL_SOCKET_LIBS) \
	$(LIBEVENT_LIBS)

test_send_control_SOURCES = test/send-control.c
test_send_control_LDADD = $(LIBCM4ALL_SOCKET_LIBS)

test_run_ua_parser_SOURCES = test/run_ua_parser.c \
	src/ua_classification.c
test_run_ua_parser_LDADD = $(AM_LDADD) $(GLIB_LIBS)

test_run_parser_cdata_SOURCES = test/t-parser-cdata.c \
	src/xml_parser.c \
	src/fifo-buffer.c src/buffered_io.c src/expansible-buffer.c \
	src/cleanup_timer.c \
	src/fd_util.c
test_run_parser_cdata_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
	$(LIBCM4ALL_DAEMON_LIBS)

test_run_css_parser_SOURCES = test/run_css_parser.c \
	src/css_parser.c \
	src/fifo-buffer.c src/buffered_io.c \
	src/cleanup_timer.c \
	src/fd_util.c
test_run_css_parser_LDADD = $(AM_LDADD) \
	libistream.a libpool.a libmemory.a \
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

valgrind: AM_CPPFLAGS += -DPOOL_LIBC_ONLY
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

doc/beng.dvi: doc/beng.tex
	@mkdir -p $(@D)
	latex -output-directory $(@D) $<
	latex -output-directory $(@D) $<

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
