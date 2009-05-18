include version.mk

DEBUG_CFLAGS = -g -DPOISON -DVALGRIND
#DEBUG_CFLAGS += -DISTREAM_POOL
CHECK_CFLAGS = -DTRACE -DDEBUG_POOL_REF
LOG_CFLAGS = -DDEBUG_POOL_GROW -DDUMP_POOL_SIZE -DCACHE_LOG
#LOG_CFLAGS += -DDUMP_POOL_UNREF
#LOG_CFLAGS += -DDUMP_WIDGET_TREE
# -DDUMP_POOL_ALLOC_ALL

CC = gcc
CFLAGS = -O0 $(DEBUG_CFLAGS) $(CHECK_CFLAGS) $(LOG_CFLAGS) -DSPLICE
CFLAGS += -funit-at-a-time
LDFLAGS = -lpthread -lrt

LARGEFILE_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE

MACHINE := $(shell uname -m)
ifeq ($(MACHINE),x86_64)
ARCH_CFLAGS = -march=athlon64
else ifeq ($(MACHINE),i686)
ARCH_CFLAGS = $(LARGEFILE_CFLAGS) -march=pentium4
else
ARCH_CFLAGS = $(LARGEFILE_CFLAGS)
endif

ifeq ($(O),1)
CFLAGS = -O3 -g -DNDEBUG -DSPLICE
endif

ifeq ($(PROFILE),1)
CFLAGS = -O3 -g -DNDEBUG -DSPLICE -DPROFILE -pg
LDFLAGS = -lc_p -pg
endif

WARNING_CFLAGS = -Wall -W -pedantic -Werror -pedantic-errors -std=gnu99 -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wfloat-equal -Wshadow -Wpointer-arith -Wbad-function-cast -Wsign-compare -Waggregate-return -Wmissing-declarations -Wmissing-noreturn -Wmissing-format-attribute -Wredundant-decls -Wnested-externs -Winline -Wdisabled-optimization -Wno-long-long -Wstrict-prototypes -Wundef

ifeq ($(ICC),1)
CC = icc
ARCH_CFLAGS = -march=pentium4
WARNING_CFLAGS = -std=gnu99 -x c -Wall -Werror -wd981
endif

MORE_CFLAGS = -DVERSION=\"$(VERSION)\" -Iinclude
MORE_CFLAGS += -I/usr/include/cm4all/libinline-0
MORE_CFLAGS += -D_GNU_SOURCE

CFLAGS += -DNO_DEFLATE

ALL_CFLAGS = $(CFLAGS) $(ARCH_CFLAGS) $(MORE_CFLAGS) $(WARNING_CFLAGS) 

LIBDAEMON_CFLAGS := $(shell pkg-config --cflags libcm4all-daemon)
LIBDAEMON_LIBS := $(shell pkg-config --libs libcm4all-daemon)

LIBEVENT_CFLAGS =
LIBEVENT_LIBS = -levent_core

LIBATTR_CFLAGS =
LIBATTR_LIBS = -lattr

SPARSE_FLAGS = -DSPARSE \
	$(MORE_CFLAGS) $(LIBDAEMON_CFLAGS) $(LIBEVENT_CFLAGS) \
	-Wdecl -Wdefault-bitfield-sign -Wdo-while -Wenum-mismatch \
	-Wnon-pointer-null -Wptr-subtraction-blows -Wreturn-void \
	-Wshadow -Wtypesign 

POOL_SOURCES = src/pool.c src/pstring.c

ISTREAM_SOURCES = \
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
	$(POOL_SOURCES)

SOURCES = src/main.c \
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
	$(ISTREAM_SOURCES) \
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
	src/http-cache.c \
	src/fcache.c \
	src/stock.c \
	src/hstock.c \
	src/abort-unref.c \
	src/abort-flag.c \
	src/tpool.c

HEADERS = $(wildcard src/*.h) $(wildcard include/beng-proxy/*.h)

ISTREAM_OBJECTS = $(patsubst %.c,%.o,$(ISTREAM_SOURCES))
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DEBUG_ARGS = -vvvvvD

.PHONY: all clean

all: src/cm4all-beng-proxy src/cm4all-beng-proxy-delegate-helper

clean:
	rm -f src/cm4all-beng-proxy src/*.a src/*.o \
		doc/beng.{log,aux,ps,pdf,html} \
		vgcore* core* gmon.out \
		test/*.o \
		test/benchmark-gmtime test/format-http-date \
		test/request-translation test/run-subst $(FILTER_TESTS) \
		test/t-istream-processor test/t-html-unescape \
		test/t-html-unescape test/t-http-server \
		test/t-http-server-mirror test/t-http-client test/t-http-util \
		test/t-http-cache test/t-processor test/run-embed \
		test/run-header-parser test/run-cookie-client \
		test/t-cookie-client test/t-html-escape test/t-parser-cdata \
		test/t-shm test/t-dpool test/t-session test/t-widget-registry \
		test/t-wembed test/run-ajp-client test/t-hashmap test/t-cache \
		test/t-cgi test/t-expansible-buffer \
		test/run-delegate \
		test/t-widget-stream
	rm -f *.{gcda,gcno,gcov} {src,test}/*.{gcda,gcno}

include demo/Makefile

src/libcm4all-istream.a: $(ISTREAM_OBJECTS)
	ar cr $@ $^

src/cm4all-beng-proxy: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS) $(LIBATTR_LIBS) -lz

src/cm4all-beng-proxy-delegate-helper: src/delegate-helper.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJECTS) src/delegate-helper.o: %.o: %.c $(HEADERS)
	$(CC) -c -o $@ $< $(ALL_CFLAGS) $(LIBEVENT_CFLAGS) $(LIBDAEMON_CFLAGS) $(LIBATTR_CFLAGS)

test/%.o: test/%.c $(HEADERS) $(wildcard test/*.h)
	$(CC) -c -o $@ $< $(ALL_CFLAGS) $(LIBEVENT_CFLAGS) $(LIBDAEMON_CFLAGS) $(LIBATTR_CFLAGS) -Isrc

test/benchmark-gmtime: test/benchmark-gmtime.o src/gmtime.o test/libcore-gmtime.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/format-http-date: test/format-http-date.o src/gmtime.o src/date.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/request-translation: test/request-translation.o src/translate.o src/pool.o src/growing-buffer.o src/socket-util.o src/stock.o src/pstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/run-subst: test/run-subst.o src/istream-subst.o src/pool.o src/istream-file.o src/fifo-buffer.o src/buffered-io.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/run-cookie-client: test/run-cookie-client.o src/cookie-client.o src/http-string.o src/header-writer.o src/growing-buffer.o src/pool.o src/pstring.o src/tpool.o src/strmap.o src/hashmap.o src/shm.o src/dpool.o src/dstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/run-header-parser: test/run-header-parser.o src/header-parser.o src/growing-buffer.o src/fifo-buffer.o src/pool.o src/pstring.o src/tpool.o src/strmap.o src/strutil.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/t-cookie-client: test/t-cookie-client.o src/cookie-client.o src/http-string.o src/header-writer.o src/growing-buffer.o src/pool.o src/pstring.o src/tpool.o src/strmap.o src/hashmap.o src/shm.o src/dpool.o src/dstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/run-embed: test/run-embed.o src/istream-subst.o src/pool.o src/pstring.o src/istream-file.o src/uri-relative.o src/uri-parser.o src/session.o src/fifo-buffer.o src/hashmap.o src/widget-class.o src/inline-widget.o src/growing-buffer.o src/widget.o src/format.o src/widget-uri.o src/istream-string.o src/args.o src/strmap.o src/uri-escape.o src/http-stock.o src/stock.o src/hstock.o src/buffered-io.o src/client-socket.o src/http-client.o src/http-body.o src/header-writer.o src/http.o src/istream-chunked.o src/istream-cat.o src/socket-util.o src/parser.o src/istream-delayed.o src/header-parser.o src/istream-memory.o src/istream-null.o src/processor.o src/istream-forward.o src/istream-tee.o src/istream-hold.o src/istream-replace.o src/widget-request.o src/widget-session.o src/embed.o src/strutil.o src/istream-dechunk.o src/cookie-client.o src/penv.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/t-parser-cdata: test/t-parser-cdata.o src/parser.o src/istream-file.o src/pool.o src/fifo-buffer.o src/buffered-io.o src/fd-util.c
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/t-html-unescape: test/t-html-unescape.o src/html-escape.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/t-html-escape: test/t-html-escape.o src/html-escape.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/t-http-server: test/t-http-server.o src/http-server.o src/http-server-send.o src/http-server-request.o src/http-server-read.o src/http-server-response.o src/fifo-buffer.o src/pool.o src/pstring.o src/buffered-io.o src/strmap.o src/hashmap.o src/header-writer.o src/istream-forward.o src/istream-string.o src/istream-dechunk.o src/istream-chunked.o src/istream-pipe.o src/istream-memory.o src/istream-cat.o src/istream-null.o src/http-body.o src/date.o src/fd-util.o src/socket-util.o src/growing-buffer.o src/http.o src/header-parser.o src/format.o src/strutil.o src/gmtime.o src/tpool.o src/istream-socketpair.o src/istream-block.o src/istream-catch.o src/sink-null.o src/event2.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

test/t-http-server-mirror: test/t-http-server-mirror.o src/http-server.o src/http-server-send.o src/http-server-request.o src/http-server-read.o src/http-server-response.o src/fifo-buffer.o src/duplex.o src/pool.o src/pstring.o src/buffered-io.o src/strmap.o src/hashmap.o src/header-writer.o src/istream-forward.o src/istream-string.o src/istream-dechunk.o src/istream-chunked.o src/istream-pipe.o src/istream-memory.o src/istream-cat.o src/istream-null.o src/http-body.o src/date.o src/fd-util.o src/socket-util.o src/growing-buffer.o src/http.o src/header-parser.o src/format.o src/strutil.o src/gmtime.o src/tpool.o src/event2.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

test/t-http-client: test/t-http-client.o src/http-client.o src/pool.o src/pstring.o src/strmap.o src/hashmap.o src/growing-buffer.o src/fifo-buffer.o src/header-writer.o src/istream-forward.o src/istream-string.o src/istream-memory.o src/istream-cat.o src/istream-fail.o src/istream-block.o src/http-body.o src/header-parser.o src/istream-chunked.o src/istream-dechunk.o src/format.o src/http.o src/strutil.o src/buffered-io.o src/fd-util.o src/socket-util.o src/istream-head.o src/istream-null.o src/istream-zero.o src/tpool.o src/event2.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

test/t-cgi: test/t-cgi.o src/cgi.o src/fork.o src/pool.o src/pstring.o src/fifo-buffer.o src/http.o src/header-parser.o src/strmap.o src/hashmap.o src/event2.o src/socket-util.o src/fd-util.o src/child.o src/buffered-io.o src/tpool.o src/growing-buffer.o src/strutil.o src/istream-file.o src/abort-flag.o src/direct.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

test/t-widget-stream: test/t-widget-stream.o \
		src/pool.o src/pstring.o \
		src/istream-forward.o \
		src/istream-null.o src/istream-memory.o src/istream-string.o \
		src/istream-delayed.o \
		src/widget-stream.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

check-widget-stream: test/t-widget-stream
	./test/t-widget-stream

test/t-http-util: test/t-http-util.o src/http-util.o src/pool.o src/pstring.o src/strutil.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/t-http-cache: test/t-http-cache.o src/pool.o src/pstring.o src/tpool.o src/strmap.o src/hashmap.o src/fifo-buffer.o src/http.o src/header-parser.o src/growing-buffer.o src/strutil.o src/istream-memory.o src/istream-string.o src/http-cache.o src/abort-unref.o src/cache.o src/header-writer.o src/http-util.o src/istream-tee.o src/date.o src/gmtime.o src/istream-null.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/t-processor: test/t-processor.o src/processor.o src/penv.o src/parser.o src/istream-replace.o src/widget.o src/widget-ref.o src/uri-relative.o src/uri-parser.o src/uri-escape.o src/strmap.o src/hashmap.o src/growing-buffer.o src/fifo-buffer.o src/pool.o src/pstring.o src/tpool.o src/istream-string.o src/istream-subst.o src/istream-file.o src/istream-cat.o src/istream-memory.o src/istream-delayed.o src/istream-hold.o src/istream-dechunk.o src/istream-chunked.o src/header-writer.o src/args.o src/buffered-io.o src/tcp-stock.o src/stock.o src/hstock.o src/client-socket.o src/socket-util.o src/fd-util.o src/format.o src/header-parser.o src/http.o src/strutil.o src/widget-request.o src/istream-tee.o src/istream-null.o src/event2.o src/failure.o src/uri-address.o src/shm.o src/dpool.o src/dstring.o src/dhashmap.o src/widget-stream.o src/expansible-buffer.o src/istream-forward.o src/istream-catch.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

FILTER_TEST_CLASSES = cat chunked dechunk pipe hold delayed subst deflate byte iconv replace replace2 html-escape fcgi
FILTER_TESTS = $(patsubst %,test/t-istream-%,$(FILTER_TEST_CLASSES))

$(filter-out %2,$(FILTER_TESTS)): test/t-istream-%: test/t-istream-%.o src/pool.o src/istream-forward.o src/istream-memory.o src/istream-string.o src/istream-byte.o src/istream-four.o src/istream-fail.o src/istream-head.o src/istream-cat.o src/istream-hold.o src/istream-%.o src/fifo-buffer.o src/format.o src/istream-later.o src/growing-buffer.o src/fd-util.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS) -lz

$(filter %2,$(FILTER_TESTS)): test/t-istream-%2: test/t-istream-%2.o src/pool.o src/istream-forward.o src/istream-memory.o src/istream-string.o src/istream-byte.o src/istream-four.o src/istream-fail.o src/istream-head.o src/istream-cat.o src/istream-hold.o src/istream-%.o src/fifo-buffer.o src/format.o src/istream-later.o src/growing-buffer.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS) -lz

test/t-istream-processor: test/t-istream-processor.o src/pool.o src/istream-forward.o src/istream-memory.o src/istream-string.o src/istream-byte.o src/istream-four.o src/istream-fail.o src/istream-head.o src/istream-cat.o src/fifo-buffer.o src/format.o src/uri-relative.o src/uri-parser.o src/uri-escape.o src/session.o src/cookie-client.o src/http-string.o src/strmap.o src/hashmap.o src/pstring.o src/penv.o src/processor.o src/widget-request.o src/istream-null.o src/istream-subst.o src/widget.o src/growing-buffer.o src/istream-replace.o src/widget-ref.o src/widget-uri.o src/args.o src/widget-session.o src/parser.o src/widget-class.o src/istream-tee.o src/istream-later.o src/widget-stream.o src/tpool.o src/istream-hold.o src/istream-delayed.o src/istream-catch.o src/rewrite-uri.o src/widget-resolver.o src/uri-address.o src/dhashmap.o src/dpool.o src/shm.o src/dstring.o src/resource-address.o src/global.o src/transformation.o src/expansible-buffer.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

$(patsubst %,check-filter-%,$(FILTER_TEST_CLASSES) processor): check-filter-%: test/t-istream-%
	exec $<

check-http-server: test/t-http-server-mirror test/t-http-server
	./test/t-http-server.py
	./test/t-http-server

check-http-client: test/t-http-client test/t-http-server-mirror
	./test/t-http-client

check-http-util: test/t-http-util
	./test/t-http-util

check-http-cache: test/t-http-cache
	./test/t-http-cache

check-cookie-client: test/run-cookie-client test/t-cookie-client
	python ./test/t-cookie-client.py
	./test/t-cookie-client

check-cgi: test/t-cgi
	./test/t-cgi

test/t-shm: test/t-shm.o src/shm.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

check-shm: test/t-shm
	./test/t-shm

test/t-dpool: test/t-dpool.o src/shm.o src/dpool.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

check-dpool: test/t-dpool
	./test/t-dpool

test/t-session: test/t-session.o src/shm.o src/session.o src/dpool.o src/dstring.o src/format.o src/dhashmap.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

check-session: test/t-session
	./test/t-session

test/t-expansible-buffer: test/t-expansible-buffer.o src/expansible-buffer.o src/pool.o src/pstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

check-expansible-buffer: test/t-expansible-buffer
	./test/t-expansible-buffer

test/t-widget-registry: test/t-widget-registry.o src/widget-registry.o src/stock.o src/pool.o src/pstring.o src/uri-address.o src/transformation.o src/tcache.o src/cache.o src/hashmap.o src/abort-unref.o src/transformation.o src/resource-address.o src/uri-relative.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

check-widget-registry: test/t-widget-registry
	./test/t-widget-registry

test/t-wembed: test/t-wembed.o src/inline-widget.o src/pool.o src/pstring.o src/widget-stream.o src/istream-delayed.o src/istream-hold.o src/istream-forward.o src/istream-null.o src/uri-parser.o src/uri-escape.o src/format.o src/global.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

check-wembed: test/t-wembed
	./test/t-wembed

test/run-ajp-client: test/run-ajp-client.o src/ajp-client.o src/pool.o src/pstring.o src/buffered-io.o src/fifo-buffer.o src/growing-buffer.o src/socket-util.o src/fd-util.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/t-hashmap: test/t-hashmap.o src/hashmap.o src/pool.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

check-hashmap: test/t-hashmap
	./test/t-hashmap

test/t-cache: test/t-cache.o src/cache.o src/pool.o src/hashmap.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

check-cache: test/t-cache
	./test/t-cache

test/run-delegate: test/run-delegate.o \
	src/delegate-stock.o src/delegate-client.o src/delegate-glue.o \
	src/hashmap.o src/hstock.o src/stock.o \
	src/defer.o \
	src/pool.o src/pstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

check: $(patsubst %,check-filter-%,$(FILTER_TEST_CLASSES) processor) \
	check-cgi \
	check-widget-stream \
	check-http-server check-http-client check-http-util \
	check-http-cache \
	check-cookie-client \
	check-shm \
	check-dpool \
	check-session \
	check-widget-registry \
	check-wembed \
	check-hashmap \
	check-cache

cov: CFLAGS += -fprofile-arcs -ftest-coverage
cov: LDFLAGS += -fprofile-arcs -ftest-coverage
cov: check
	-cp -s {src,test}/*.{gcda,gcno} .
	gcov src/*.c

debug: src/cm4all-beng-proxy
	rm -f /tmp/cm4all-beng-proxy.gdb
	echo -en "handle SIGPIPE noprint nostop\nrun\n" >/tmp/cm4all-beng-proxy.gdb
	LD_LIBRARY_PATH=/usr/lib/debug:$(LD_LIBRARY_PATH) gdb -x /tmp/cm4all-beng-proxy.gdb --args $< $(DEBUG_ARGS)

profile: CFLAGS = -O3 -DNDEBUG -DSPLICE -DPROFILE -DNO_ACCESS_LOG -g -pg
profile: LDFLAGS = -lc_p -pg
profile: src/cm4all-beng-proxy
	./src/cm4all-beng-proxy -D -u max -p 8080

# -DNO_DATE_HEADER -DNO_XATTR -DNO_LAST_MODIFIED_HEADER
benchmark: CFLAGS = -O3 -DNDEBUG -DALWAYS_INLINE -DNO_ACCESS_LOG -g
benchmark: src/cm4all-beng-proxy
	./src/cm4all-beng-proxy -D -u max -p 8080

valgrind: DEBUG_CFLAGS += -DPOOL_LIBC_ONLY
valgrind: src/cm4all-beng-proxy
	valgrind --show-reachable=yes --leak-check=yes ./src/cm4all-beng-proxy $(DEBUG_ARGS)

$(addprefix sparse-,$(SOURCES)): sparse-%: %
	sparse $(SPARSE_FLAGS) $<

sparse: $(addprefix sparse-,$(SOURCES))

doc/beng.pdf: doc/beng.tex
	cd $(dir $<) && pdflatex $(notdir $<) && pdflatex $(notdir $<)

doc/beng.dvi: doc/beng.tex
	cd $(dir $<) && latex $(notdir $<)

# upload the demo widgets to cfatest01
upload:
	scp demo/cgi-bin/*.py demo/cgi-bin/*.sh demo/base.html demo/nested_base.html demo/container.html cfatest01:/var/www/vol1/HTO01F/LY/YL/XT/pr_0001/widgets/
