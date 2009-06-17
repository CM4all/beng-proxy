AUTOMAKE_OPTIONS = foreign 1.10 dist-bzip2

#
# Compiler/linker flags
#

LIBEVENT_CFLAGS =
LIBEVENT_LIBS = -levent_core

LIBATTR_CFLAGS =
LIBATTR_LIBS = -lattr

LIBZ_CFLAGS =
LIBZ_LIBS = -lz

AM_CFLAGS += 
AM_CPPFLAGS += \
	$(LIBCM4ALL_DAEMON_CFLAGS) \
	$(LIBCM4ALL_INLINE_CFLAGS) \
	$(LIBEVENT_CFLAGS) \
	$(LIBATTR_CFLAGS) \
	$(LIBZ_CFLAGS) \
	-DSPLICE \
	-D_GNU_SOURCE
INCLUDES = -Isrc -Iinclude

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

POOL_SRC = src/pool.c src/pstring.c

ISTREAM_SRC = \
	src/istream-forward.c \
	src/istream-memory.c \
	src/istream-null.c \
	src/istream-zero.c \
	src/istream-block.c \
	src/istream-string.c \
	src/istream-file.c \
	src/istream-chunked.c \
	src/istream-dechunk.c \
	src/istream-fcgi.c \
	src/istream-cat.c \
	src/istream-pipe.c \
	src/istream-delayed.c \
	src/istream-hold.c \
	src/istream-deflate.c \
	src/istream-subst.c \
	src/istream-byte.c \
	src/istream-four.c \
	src/istream-iconv.c \
	src/istream-trace.c \
	src/istream-fail.c \
	src/istream-catch.c \
	src/istream-html-escape.c \
	src/istream-later.c \
	src/istream-head.c \
	src/istream-tee.c \
	src/istream-replace.c \
	src/istream-socketpair.c \
	src/format.c \
	src/fifo-buffer.c \
	src/sink-null.c \
	$(POOL_SRC)

SRC = src/main.c \
	src/cmdline.c \
	src/global.c \
	src/event2.c \
	src/child.c \
	src/defer.c \
	src/worker.c \
	src/shm.c \
	src/dpool.c \
	src/dstring.c \
	src/session.c \
	src/cookie-client.c \
	src/cookie-server.c \
	src/connection.c \
	src/direct.c \
	src/translate.c \
	src/transformation.c \
	src/tcache.c \
	src/request.c \
	src/response.c \
	src/handler.c \
	src/file-handler.c \
	src/cgi-handler.c \
	src/proxy-handler.c \
	src/widget.c \
	src/widget-class.c \
	src/widget-ref.c \
	src/widget-session.c \
	src/widget-uri.c \
	src/widget-request.c \
	src/widget-stream.c \
	src/widget-registry.c \
	src/widget-resolver.c \
	src/proxy-widget.c \
	src/html-escape.c \
	src/processor.c \
	src/penv.c \
	src/parser.c \
	src/rewrite-uri.c \
	src/get.c \
	src/static-file.c \
	src/widget-http.c \
	src/inline-widget.c \
	src/frame.c \
	src/fd-util.c \
	src/socket-util.c \
	src/address.c \
	src/resource-address.c \
	src/resource-tag.c \
	src/listener.c \
	src/client-socket.c \
	src/buffered-io.c \
	src/header-parser.c \
	src/header-writer.c \
	src/http.c \
	src/http-string.c \
	src/http-body.c \
	src/http-server.c \
	src/http-server-send.c \
	src/http-server-request.c \
	src/http-server-read.c \
	src/http-server-response.c \
	src/http-client.c \
	src/http-util.c \
	src/ajp-client.c \
	src/access-log.c \
	src/tcp-stock.c \
	src/http-request.c \
	src/ajp-request.c \
	src/pipe.c \
	src/cgi.c \
	src/fcgi-stock.c \
	src/fcgi-client.c \
	src/fcgi-request.c \
	src/growing-buffer.c \
	src/expansible-buffer.c \
	src/gb-io.c \
	src/duplex.c \
	$(ISTREAM_SRC) \
	src/fork.c \
	src/delegate-stock.c \
	src/delegate-client.c \
	src/delegate-glue.c \
	src/delegate-get.c \
	src/uri-relative.c \
	src/uri-parser.c \
	src/uri-address.c \
	src/uri-escape.c \
	src/failure.c \
	src/args.c \
	src/gmtime.c \
	src/date.c \
	src/strutil.c \
	src/hashmap.c \
	src/strmap.c \
	src/dhashmap.c \
	src/cache.c \
	src/istream-unlock.c \
	src/http-cache.c \
	src/fcache.c \
	src/stock.c \
	src/hstock.c \
	src/abort-unref.c \
	src/abort-flag.c \
	src/tpool.c

#
# Programs
#

bin_PROGRAMS = src/cm4all-beng-proxy \
	src/cm4all-beng-proxy-delegate-helper

src_cm4all_beng_proxy_SOURCES = $(SRC)
src_cm4all_beng_proxy_LDADD = \
	$(LIBCM4ALL_DAEMON_LIBS) \
	$(LIBCM4ALL_INLINE_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBATTR_LIBS) \
	$(LIBZ_LIBS)
src_cm4all_beng_proxy_LDFLAGS =

if GPROF
AM_CFLAGS += -pg
AM_CPPFLAGS += -DPROFILE
src_cm4all_beng_proxy_LDADD += -lc_p
src_cm4all_beng_proxy_LDFLAGS += -pg
endif

src_cm4all_beng_proxy_delegate_helper_SOURCES = src/delegate-helper.c

#
# Libraries
#

lib_LIBRARIES = src/libcm4all-istream.a
src_libcm4all_istream_a_SOURCES = $(ISTREAM_SRC)

#
# Test suite
#

FILTER_TESTS = test/t-istream-cat test/t-istream-chunked test/t-istream-dechunk \
	test/t-istream-pipe test/t-istream-hold test/t-istream-delayed \
	test/t-istream-subst test/t-istream-deflate test/t-istream-byte \
	test/t-istream-iconv test/t-istream-replace test/t-istream-replace2 \
	test/t-istream-html-escape test/t-istream-fcgi \
	test/t-istream-processor

noinst_PROGRAMS = \
	test/t-hashmap test/t-cache test/t-tcache \
	test/t-resource-address \
	test/t-uri-compress \
	test/t-expansible-buffer \
	$(FILTER_TESTS) \
	test/t-cgi test/t-widget-stream \
	test/t-http-server-mirror test/t-http-server \
	test/t-http-client test/t-http-util \
	test/t-http-cache \
	test/t-cookie-client \
	test/t-shm test/t-dpool test/t-session \
	test/t-widget-registry \
	test/t-wembed test/t-widget-http \
	test/benchmark-gmtime test/format-http-date \
	test/request-translation \
	test/run-subst \
	test/run-cookie-client \
	test/run-header-parser \
	test/run-html-unescape test/run-html-escape \
	test/run-processor \
	test/run-ajp-client \
	test/run-delegate \
	test/run-parser-cdata

TESTS = test/t-hashmap test/t-cache test/t-tcache \
	test/t-resource-address \
	test/t-uri-compress \
	test/t-expansible-buffer \
	$(FILTER_TESTS) \
	test/t-cgi test/t-widget-stream \
	test/t-http-server.py test/t-http-server \
	test/t-http-client test/t-http-util \
	test/t-http-cache \
	test/t-cookie-client.py test/t-cookie-client \
	test/t-shm test/t-dpool test/t-session \
	test/t-widget-registry \
	test/t-wembed test/t-widget-http

test_t_hashmap_SOURCES = test/t-hashmap.c src/hashmap.c $(POOL_SRC)
test_t_hashmap_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

test_t_cache_SOURCES = test/t-cache.c src/cache.c src/hashmap.c $(POOL_SRC)
test_t_cache_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_t_tcache_SOURCES = test/t-tcache.c src/tcache.c src/cache.c \
	src/hashmap.c \
	src/resource-address.c src/uri-address.c src/uri-relative.c \
	src/transformation.c \
	$(POOL_SRC)
test_t_tcache_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) -lrt

test_t_resource_address_SOURCES = test/t-resource-address.c \
	src/resource-address.c \
	src/uri-address.c src/uri-relative.c \
	$(POOL_SRC)
test_t_resource_address_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

test_t_uri_compress_SOURCES = test/t-uri-compress.c \
	src/uri-relative.c \
	$(POOL_SRC)
test_t_uri_compress_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

test_t_expansible_buffer_SOURCES = test/t-expansible-buffer.c \
	src/expansible-buffer.c \
	$(POOL_SRC)
test_t_expansible_buffer_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

T_ISTREAM_FILTER_SRC = $(ISTREAM_SRC) \
	src/buffered-io.c src/event2.c \
	src/growing-buffer.c src/fd-util.c src/socket-util.c
T_ISTREAM_FILTER_LIB = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) -lz

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

test_t_istream_html_escape_SOURCES = test/t-istream-html-escape.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_html_escape_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_fcgi_SOURCES = test/t-istream-cat.c $(T_ISTREAM_FILTER_SRC)
test_t_istream_fcgi_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_istream_processor_SOURCES = test/t-istream-processor.c \
	src/uri-relative.c src/uri-parser.c src/uri-escape.c \
	src/session.c src/cookie-client.c \
	src/http-string.c \
	src/strmap.c src/hashmap.c \
	src/penv.c src/processor.c \
	src/widget-request.c \
	src/widget.c src/widget-ref.c src/widget-uri.c src/args.c \
	src/widget-session.c src/parser.c src/widget-class.c \
	src/widget-stream.c \
	src/tpool.c src/rewrite-uri.c src/widget-resolver.c \
	src/uri-address.c src/dhashmap.c src/dpool.c src/shm.c src/dstring.c \
	src/resource-address.c src/global.c src/transformation.c \
	src/expansible-buffer.c \
	$(T_ISTREAM_FILTER_SRC)
test_t_istream_processor_LDADD = $(T_ISTREAM_FILTER_LIB)

test_t_cgi_SOURCES = test/t-cgi.c \
	src/cgi.c src/fork.c \
	$(POOL_SRC) src/tpool.c \
	src/fifo-buffer.c src/buffered-io.c src/growing-buffer.c \
	src/http.c src/header-parser.c \
	src/strmap.c src/hashmap.c \
	src/event2.c \
	src/socket-util.c src/fd-util.c \
	src/child.c \
	src/strutil.c \
	src/istream-file.c src/abort-flag.c src/direct.c
test_t_cgi_LDADD = $(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS)

test_t_widget_stream_SOURCES = test/t-widget-stream.c \
	$(POOL_SRC) \
	src/istream-forward.c src/istream-null.c src/istream-memory.c \
	src/istream-string.c src/istream-delayed.c \
	src/widget-stream.c
test_t_widget_stream_LDADD = $(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS)

test_t_http_server_SOURCES = test/t-http-server.c \
	src/http-server.c src/http-server-send.c src/http-server-request.c \
	src/http-server-read.c src/http-server-response.c \
	src/fifo-buffer.c \
	$(POOL_SRC) \
	src/buffered-io.c \
	src/strmap.c src/hashmap.c \
	src/header-writer.c \
	src/istream-forward.c src/istream-string.c src/istream-dechunk.c \
	src/istream-chunked.c src/istream-pipe.c src/istream-memory.c \
	src/istream-cat.c src/istream-null.c \
	src/http-body.c src/date.c \
	src/fd-util.c src/socket-util.c \
	src/growing-buffer.c \
	src/http.c src/header-parser.c src/format.c \
	src/strutil.c \
	src/gmtime.c \
	src/tpool.c \
	src/istream-socketpair.c src/istream-block.c src/istream-catch.c \
	src/sink-null.c src/event2.c
test_t_http_server_LDADD = $(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS)

test_t_http_server_mirror_SOURCES = test/t-http-server-mirror.c \
	src/http-server.c src/http-server-send.c src/http-server-request.c \
	src/http-server-read.c src/http-server-response.c \
	src/fifo-buffer.c \
	src/duplex.c \
	$(POOL_SRC) \
	src/buffered-io.c \
	src/strmap.c src/hashmap.c \
	src/header-writer.c \
	src/istream-forward.c src/istream-string.c src/istream-dechunk.c \
	src/istream-chunked.c src/istream-pipe.c src/istream-memory.c \
	src/istream-cat.c src/istream-null.c \
	src/http-body.c src/date.c \
	src/fd-util.c src/socket-util.c \
	src/growing-buffer.c \
	src/http.c src/header-parser.c src/format.c \
	src/strutil.c src/gmtime.c src/tpool.c src/event2.c
test_t_http_server_mirror_LDADD = $(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS)

test_t_http_client_SOURCES = test/t-http-client.c \
	src/http-client.c \
	$(POOL_SRC) \
	src/strmap.c src/hashmap.c \
	src/growing-buffer.c src/fifo-buffer.c \
	src/header-writer.c \
	src/istream-forward.c src/istream-string.c src/istream-memory.c \
	src/istream-cat.c src/istream-fail.c src/istream-block.c \
	src/istream-chunked.c src/istream-dechunk.c \
	src/istream-head.c src/istream-null.c src/istream-zero.c \
	src/http-body.c src/header-parser.c \
	src/format.c src/http.c src/strutil.c \
	src/buffered-io.c src/fd-util.c src/socket-util.c \
	src/tpool.c src/event2.c
test_t_http_client_LDADD = $(LIBEVENT_LIBS) $(LIBCM4ALL_DAEMON_LIBS)

test_t_http_util_SOURCES = test/t-http-util.c \
	$(POOL_SRC) \
	src/http-util.c src/strutil.c
test_t_http_util_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

test_t_http_cache_SOURCES = test/t-http-cache.c \
	$(POOL_SRC) src/tpool.c \
	src/strmap.c src/hashmap.c \
	src/fifo-buffer.c src/http.c src/header-parser.c \
	src/growing-buffer.c \
	src/strutil.c \
	src/istream-memory.c src/istream-string.c \
	src/http-cache.c src/abort-unref.c src/cache.c \
	src/header-writer.c src/http-util.c \
	src/istream-tee.c \
	src/date.c src/gmtime.c \
	src/istream-null.c src/istream-unlock.c src/istream-forward.c
test_t_http_cache_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_t_cookie_client_SOURCES = test/t-cookie-client.c \
	$(POOL_SRC) src/tpool.c \
	src/cookie-client.c \
	src/http-string.c src/header-writer.c src/growing-buffer.c \
	src/strmap.c src/hashmap.c \
	src/shm.c src/dpool.c src/dstring.c
test_t_cookie_client_LDADD = $(LIBCM4ALL_DAEMON_LIBS) -lrt

test_t_shm_SOURCES = test/t-shm.c src/shm.c
test_t_shm_LDADD = $(LIBCM4ALL_DAEMON_LIBS) -lrt

test_t_dpool_SOURCES = test/t-dpool.c src/shm.c src/dpool.c
test_t_dpool_LDADD = $(LIBCM4ALL_DAEMON_LIBS) -lrt

test_t_session_SOURCES = test/t-session.c src/shm.c \
	src/dpool.c src/dstring.c src/dhashmap.c \
	src/format.c \
	src/session.c
test_t_session_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS) -lrt

test_t_widget_registry_SOURCES = test/t-widget-registry.c \
	src/widget-registry.c src/stock.c \
	$(POOL_SRC) \
	src/uri-address.c src/transformation.c \
	src/tcache.c src/cache.c \
	src/hashmap.c src/abort-unref.c \
	src/resource-address.c src/uri-relative.c
test_t_widget_registry_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_t_wembed_SOURCES = test/t-wembed.c \
	src/inline-widget.c \
	$(POOL_SRC) \
	src/widget-stream.c \
	src/istream-delayed.c src/istream-hold.c src/istream-forward.c \
	src/istream-null.c \
	src/uri-parser.c src/uri-escape.c src/format.c src/global.c
test_t_wembed_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

test_t_widget_http_SOURCES = test/t-widget-http.c \
	src/widget-http.c src/widget-uri.c src/widget-request.c \
	src/widget-session.c src/session.c \
	src/args.c src/uri-escape.c src/format.c \
	src/resource-address.c src/uri-address.c src/uri-relative.c \
	src/resource-tag.c \
	src/cookie-client.c \
	src/header-parser.c \
	src/strmap.c src/hashmap.c \
	src/growing-buffer.c src/fifo-buffer.c \
	src/strutil.c src/http-string.c src/http-util.c \
	src/istream-string.c src/istream-iconv.c src/istream-html-escape.c \
	src/istream-cat.c src/istream-forward.c src/istream-memory.c \
	src/istream-null.c \
	src/transformation.c \
	src/dpool.c src/dstring.c src/dhashmap.c src/shm.c \
	$(POOL_SRC) src/tpool.c
test_t_widget_http_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_benchmark_gmtime_SOURCES = test/benchmark-gmtime.c \
	src/gmtime.c test/libcore-gmtime.c

test_format_http_date_SOURCES = test/format-http-date.c \
	src/gmtime.c src/date.c

test_request_translation_SOURCES = test/request-translation.c \
	src/translate.c src/growing-buffer.c src/gb-io.c \
	src/tcp-stock.c src/failure.c src/client-socket.c \
	src/fd-util.c src/socket-util.c \
	src/stock.c src/hstock.c src/hashmap.c \
	src/abort-unref.c \
	src/uri-address.c \
	$(POOL_SRC)
test_request_translation_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_run_subst_SOURCES = test/run-subst.c \
	src/istream-subst.c src/istream-file.c \
	src/fifo-buffer.c src/buffered-io.c \
	src/fd-util.c \
	$(POOL_SRC)
test_run_subst_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

test_run_cookie_client_SOURCES = test/run-cookie-client.c \
	src/cookie-client.c src/http-string.c \
	src/header-writer.c src/growing-buffer.c \
	src/tpool.c src/strmap.c src/hashmap.c \
	src/shm.c src/dpool.c src/dstring.c \
	$(POOL_SRC)
test_run_cookie_client_LDADD = $(LIBCM4ALL_DAEMON_LIBS) -lrt

test_run_header_parser_SOURCES = test/run-header-parser.c \
	src/header-parser.c src/growing-buffer.c src/fifo-buffer.c \
	src/tpool.c src/strmap.c src/strutil.c \
	src/hashmap.c \
	$(POOL_SRC)
test_run_header_parser_LDADD = $(LIBCM4ALL_DAEMON_LIBS)

test_run_html_unescape_SOURCES = test/t-html-unescape.c src/html-escape.c
test_run_html_escape_SOURCES = test/t-html-escape.c src/html-escape.c

test_run_processor_SOURCES = test/t-processor.c \
	src/processor.c src/penv.c src/parser.c \
	src/istream-replace.c \
	src/widget.c src/widget-ref.c \
	src/uri-relative.c src/uri-parser.c src/uri-escape.c \
	src/strmap.c src/hashmap.c \
	src/growing-buffer.c src/fifo-buffer.c \
	src/tpool.c \
	src/istream-string.c src/istream-subst.c src/istream-file.c \
	src/istream-cat.c src/istream-memory.c src/istream-delayed.c \
	src/istream-hold.c src/istream-dechunk.c src/istream-chunked.c \
	src/header-writer.c src/args.c src/buffered-io.c \
	src/tcp-stock.c src/stock.c src/hstock.c \
	src/client-socket.c src/socket-util.c src/fd-util.c \
	src/format.c src/header-parser.c src/http.c src/strutil.c \
	src/widget-request.c \
	src/istream-tee.c src/istream-null.c \
	src/event2.c src/failure.c src/uri-address.c \
	src/shm.c src/dpool.c src/dstring.c src/dhashmap.c \
	src/widget-stream.c src/expansible-buffer.c \
	src/istream-forward.c src/istream-catch.c \
	$(POOL_SRC)
test_run_processor_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_run_ajp_client_SOURCES = test/run-ajp-client.c \
	src/ajp-client.c src/buffered-io.c src/fifo-buffer.c \
	src/growing-buffer.c src/socket-util.c src/fd-util.c \
	src/event2.c \
	$(POOL_SRC)
test_run_ajp_client_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_run_delegate_SOURCES = test/run-delegate.c \
	src/delegate-stock.c src/delegate-client.c src/delegate-glue.c \
	src/hashmap.c src/hstock.c src/stock.c \
	src/defer.c \
	$(POOL_SRC)
test_run_delegate_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

test_run_parser_cdata_SOURCES = test/t-parser-cdata.c \
	src/parser.c src/istream-file.c \
	src/fifo-buffer.c src/buffered-io.c src/expansible-buffer.c \
	src/fd-util.c \
	$(POOL_SRC)
test_run_parser_cdata_LDADD = $(LIBCM4ALL_DAEMON_LIBS) $(LIBEVENT_LIBS)

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
# Running beng-proxy
#

DEBUG_ARGS = -vvvvvD

debug: src/cm4all-beng-proxy
	rm -f /tmp/cm4all-beng-proxy.gdb
	echo -en "handle SIGPIPE noprint nostop\nrun\n" >/tmp/cm4all-beng-proxy.gdb
	LD_LIBRARY_PATH=/usr/lib/debug:$(LD_LIBRARY_PATH) gdb -x /tmp/cm4all-beng-proxy.gdb --args $< $(DEBUG_ARGS)

valgrind: AM_CPPFLAGS += -DPOOL_LIBC_ONLY
valgrind: src/cm4all-beng-proxy
	valgrind --show-reachable=yes --leak-check=yes ./src/cm4all-beng-proxy $(DEBUG_ARGS)

.PHONY: debug valgrind

#profile: CFLAGS = -O3 -DNDEBUG -DSPLICE -DPROFILE -DNO_ACCESS_LOG -g -pg
#profile: LDFLAGS = -lc_p -pg
#profile: src/cm4all-beng-proxy
#	./src/cm4all-beng-proxy -D -u max -p 8080
#
## -DNO_DATE_HEADER -DNO_XATTR -DNO_LAST_MODIFIED_HEADER
#benchmark: CFLAGS = -O3 -DNDEBUG -DALWAYS_INLINE -DNO_ACCESS_LOG -g
#benchmark: src/cm4all-beng-proxy
#	./src/cm4all-beng-proxy -D -u max -p 8080

#
# Documentation
#

if DOCUMENTATION
doc/beng.pdf: doc/beng.tex
	cd $(dir $<) && pdflatex $(notdir $<) && pdflatex $(notdir $<)

doc/beng.dvi: doc/beng.tex
	cd $(dir $<) && latex $(notdir $<)
endif

#
# Demo
#

COMAC = /usr/bin/cm4all-comac --quiet-output

.PHONY: upload
upload:
	scp demo/cgi-bin/*.py demo/cgi-bin/*.sh demo/base.html demo/nested_base.html demo/container.html cfatest01:/var/www/vol1/HTO01F/LY/YL/XT/pr_0001/widgets/

COMA_SOURCES = $(wildcard demo/coma/*.cma)
COMA_CLASSES = $(patsubst %.cma,%.cls,$(COMA_SOURCES))

demo-all: $(COMA_CLASSES)

demo-clean:
	rm -f $(COMA_CLASSES)

$(COMA_CLASSES): %.cls: %.cma
	$(COMAC) $<
