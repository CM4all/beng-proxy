include version.mk

DEBUG_CFLAGS = -g -DPOISON -DVALGRIND
#DEBUG_CFLAGS += -DISTREAM_POOL
CHECK_CFLAGS = -DTRACE -DDEBUG_POOL_REF
LOG_CFLAGS = -DDEBUG_POOL_GROW -DDUMP_POOL_SIZE -DCACHE_LOG
#LOG_CFLAGS += -DDUMP_POOL_UNREF
# -DDUMP_POOL_ALLOC_ALL

CC = gcc
CFLAGS = -O0 $(DEBUG_CFLAGS) $(CHECK_CFLAGS) $(LOG_CFLAGS) -DSPLICE
CFLAGS += -funit-at-a-time
LDFLAGS = -lpthread

LARGEFILE_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

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
	src/istream-cat.c \
	src/istream-pipe.c \
	src/istream-delayed.c \
	src/istream-hold.c \
	src/istream-deflate.c \
	src/istream-subst.c \
	src/istream-byte.c \
	src/istream-iconv.c \
	src/istream-trace.c \
	src/istream-fail.c \
	src/istream-catch.c \
	src/istream-later.c \
	src/istream-head.c \
	src/istream-tee.c \
	src/istream-replace.c \
	src/format.c \
	src/fifo-buffer.c \
	$(POOL_SOURCES)

SOURCES = src/main.c \
	src/cmdline.c \
	src/child.c \
	src/worker.c \
	src/shm.c \
	src/dpool.c \
	src/dstring.c \
	src/session.c \
	src/cookie-client.c \
	src/cookie-server.c \
	src/connection.c \
	src/translate.c \
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
	src/js-filter.c \
	src/processor.c \
	src/penv.c \
	src/parser.c \
	src/rewrite-uri.c \
	src/get.c \
	src/static-file.c \
	src/widget-http.c \
	src/wembed.c \
	src/frame.c \
	src/google-gadget.c \
	src/google-gadget-msg.c \
	src/fd-util.c \
	src/socket-util.c \
	src/address.c \
	src/resource-address.c \
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
	src/access-log.c \
	src/http-stock.c \
	src/http-request.c \
	src/cgi.c \
	src/filter.c \
	src/growing-buffer.c \
	src/gb-io.c \
	src/duplex.c \
	$(ISTREAM_SOURCES) \
	src/fork.c \
	src/uri-relative.c \
	src/uri-parser.c \
	src/uri-address.c \
	src/uri-escape.c \
	src/args.c \
	src/gmtime.c \
	src/date.c \
	src/strutil.c \
	src/hashmap.c \
	src/strmap.c \
	src/dhashmap.c \
	src/cache.c \
	src/http-cache.c \
	src/stock.c \
	src/hstock.c \
	src/abort-unref.c \
	src/tpool.c

HEADERS = $(wildcard src/*.h) $(wildcard include/beng-proxy/*.h)

ISTREAM_OBJECTS = $(patsubst %.c,%.o,$(ISTREAM_SOURCES))
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

DEBUG_ARGS = -vvvvvD

.PHONY: all clean

all: src/cm4all-beng-proxy

clean:
	rm -f src/cm4all-beng-proxy src/*.a src/*.o doc/beng.{log,aux,ps,pdf,html} vgcore* core* gmon.out test/*.o test/benchmark-gmtime test/format-http-date test/request-translation test/js test/run-subst $(FILTER_TESTS) test/t-istream-js test/t-istream-processor test/t-html-unescape test/t-html-unescape test/t-http-server-mirror test/t-http-client test/t-processor test/run-google-gadget test/run-embed test/run-header-parser test/run-cookie-client test/t-cookie-client test/t-html-escape test/t-istream-replace test/t-parser-cdata test/t-shm test/t-dpool test/t-session test/t-widget-registry test/t-wembed

include demo/Makefile

src/libcm4all-istream.a: $(ISTREAM_OBJECTS)
	ar cr $@ $^

src/cm4all-beng-proxy: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS) $(LIBATTR_LIBS) -lz

$(OBJECTS): %.o: %.c $(HEADERS)
	$(CC) -c -o $@ $< $(ALL_CFLAGS) $(LIBEVENT_CFLAGS) $(LIBDAEMON_CFLAGS) $(LIBATTR_CFLAGS)

test/%.o: test/%.c $(HEADERS) $(wildcard test/*.h)
	$(CC) -c -o $@ $< $(ALL_CFLAGS) $(LIBEVENT_CFLAGS) $(LIBDAEMON_CFLAGS) $(LIBATTR_CFLAGS) -Isrc

test/benchmark-gmtime: test/benchmark-gmtime.o src/gmtime.o test/libcore-gmtime.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/format-http-date: test/format-http-date.o src/gmtime.o src/date.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/request-translation: test/request-translation.o src/translate.o src/pool.o src/growing-buffer.o src/socket-util.o src/stock.o src/pstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/js: test/js.o src/js-filter.o src/pool.o src/istream-file.o src/fifo-buffer.o src/buffered-io.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/run-subst: test/run-subst.o src/istream-subst.o src/pool.o src/istream-file.o src/fifo-buffer.o src/buffered-io.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/run-cookie-client: test/run-cookie-client.o src/cookie-client.o src/http-string.o src/header-writer.o src/growing-buffer.o src/pool.o src/pstring.o src/tpool.o src/strmap.o src/hashmap.o src/shm.o src/dpool.o src/dstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/run-header-parser: test/run-header-parser.o src/header-parser.o src/growing-buffer.o src/fifo-buffer.o src/pool.o src/pstring.o src/tpool.o src/strmap.o src/strutil.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/t-cookie-client: test/t-cookie-client.o src/cookie-client.o src/http-string.o src/header-writer.o src/growing-buffer.o src/pool.o src/pstring.o src/tpool.o src/strmap.o src/hashmap.o src/shm.o src/dpool.o src/dstring.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/run-embed: test/run-embed.o src/istream-subst.o src/pool.o src/pstring.o src/istream-file.o src/uri-relative.o src/uri-parser.o src/session.o src/fifo-buffer.o src/hashmap.o src/widget-class.o src/wembed.o src/growing-buffer.o src/widget.o src/format.o src/widget-uri.o src/istream-string.o src/args.o src/strmap.o src/uri-escape.o src/http-stock.o src/stock.o src/hstock.o src/buffered-io.o src/client-socket.o src/http-client.o src/http-body.o src/header-writer.o src/http.o src/istream-chunked.o src/istream-cat.o src/socket-util.o src/parser.o src/istream-delayed.o src/header-parser.o src/istream-memory.o src/istream-null.o src/processor.o src/js-filter.o src/istream-forward.o src/istream-tee.o src/istream-hold.o src/istream-replace.o src/widget-request.o src/widget-session.o src/embed.o src/strutil.o src/istream-dechunk.o src/cookie-client.o src/penv.o src/google-gadget.o src/google-gadget-msg.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/run-google-gadget: test/run-google-gadget.o src/istream-subst.o src/pool.o src/pstring.o src/istream-file.o src/uri-relative.o src/uri-parser.o src/session.o src/fifo-buffer.o src/hashmap.o src/widget-class.o src/wembed.o src/growing-buffer.o src/widget.o src/format.o src/widget-uri.o src/istream-string.o src/args.o src/strmap.o src/uri-escape.o src/http-stock.o src/stock.o src/hstock.o src/buffered-io.o src/client-socket.o src/http-client.o src/http-body.o src/header-writer.o src/http.o src/istream-chunked.o src/istream-cat.o src/socket-util.o src/google-gadget.o src/parser.o src/istream-delayed.o src/header-parser.o src/istream-memory.o src/istream-null.o src/processor.o src/js-filter.o src/istream-tee.o src/istream-hold.o src/istream-replace.o src/widget-request.o src/widget-session.o src/embed.o src/strutil.o src/google-gadget-msg.o src/istream-dechunk.o src/cookie-client.o src/penv.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/t-parser-cdata: test/t-parser-cdata.o src/parser.o src/istream-file.o src/pool.o src/fifo-buffer.o src/buffered-io.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

test/t-html-unescape: test/t-html-unescape.o src/html-escape.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/t-html-escape: test/t-html-escape.o src/html-escape.o
	$(CC) -o $@ $^ $(LDFLAGS)

test/t-http-server-mirror: test/t-http-server-mirror.o src/http-server.o src/http-server-send.o src/http-server-request.o src/http-server-read.o src/http-server-response.o src/fifo-buffer.o src/duplex.o src/pool.o src/pstring.o src/buffered-io.o src/strmap.o src/hashmap.o src/header-writer.o src/istream-forward.o src/istream-string.o src/istream-dechunk.o src/istream-chunked.o src/istream-pipe.o src/istream-memory.o src/istream-cat.o src/http-body.o src/date.o src/fd-util.o src/socket-util.o src/growing-buffer.o src/http.o src/header-parser.o src/format.o src/strutil.o src/gmtime.o src/tpool.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

test/t-http-client: test/t-http-client.o src/http-client.o src/pool.o src/pstring.o src/strmap.o src/hashmap.o src/growing-buffer.o src/fifo-buffer.o src/header-writer.o src/istream-forward.o src/istream-string.o src/istream-memory.o src/istream-cat.o src/http-body.o src/header-parser.o src/istream-chunked.o src/istream-dechunk.o src/format.o src/http.o src/strutil.o src/buffered-io.o src/fd-util.o src/socket-util.o src/istream-head.o src/istream-zero.o src/tpool.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

test/t-processor: test/t-processor.o src/processor.o src/penv.o src/parser.o src/istream-replace.o src/widget.o src/widget-class.o src/widget-ref.o src/widget-uri.o src/widget-session.o src/embed.o src/wembed.o src/uri-relative.o src/uri-parser.o src/uri-escape.o src/strmap.o src/hashmap.o src/growing-buffer.o src/fifo-buffer.o src/pool.o src/pstring.o src/istream-string.o src/istream-subst.o src/istream-file.o src/istream-cat.o src/istream-memory.o src/istream-delayed.o src/istream-hold.o src/istream-dechunk.o src/istream-chunked.o src/session.o src/cookie-client.o src/header-writer.o src/args.o src/buffered-io.o src/http-stock.o src/stock.o src/hstock.o src/js-filter.o src/client-socket.o src/http-client.o src/http-body.o src/socket-util.o src/format.o src/header-parser.o src/http.o src/strutil.o src/widget-request.o src/google-gadget.o src/google-gadget-msg.o src/istream-tee.o src/istream-null.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

FILTER_TEST_CLASSES = cat chunked dechunk pipe hold delayed subst deflate byte iconv
FILTER_TESTS = $(patsubst %,test/t-istream-%,$(FILTER_TEST_CLASSES))

$(FILTER_TESTS): test/t-istream-%: test/t-istream-%.o src/pool.o src/istream-forward.o src/istream-memory.o src/istream-string.o src/istream-byte.o src/istream-fail.o src/istream-head.o src/istream-cat.o src/istream-%.o src/fifo-buffer.o src/format.o src/istream-later.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS) -lz

test/t-istream-processor: test/t-istream-processor.o src/pool.o src/istream-forward.o src/istream-memory.o src/istream-string.o src/istream-byte.o src/istream-fail.o src/istream-head.o src/istream-cat.o src/fifo-buffer.o src/format.o src/uri-relative.o src/uri-parser.o src/uri-escape.o src/session.o src/cookie-client.o src/http-string.o src/strmap.o src/hashmap.o src/pstring.o src/penv.o src/processor.o src/widget-request.o src/istream-subst.o src/widget.o src/growing-buffer.o src/js-filter.o src/istream-replace.o src/widget-ref.o src/widget-uri.o src/args.o src/widget-session.o src/parser.o src/widget-class.o src/istream-tee.o src/istream-later.o src/widget-stream.o src/tpool.o src/istream-hold.o src/istream-delayed.o src/istream-catch.o src/rewrite-uri.o src/widget-resolver.o src/uri-address.o src/dhashmap.o src/dpool.o src/shm.o src/dstring.o src/resource-address.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

test/t-istream-js: test/t-istream-js.o src/pool.o src/istream-forward.o src/istream-memory.o src/istream-string.o src/istream-byte.o src/istream-fail.o src/istream-head.o src/istream-cat.o src/js-filter.o src/fifo-buffer.o src/format.o src/istream-later.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS) $(LIBEVENT_LIBS)

$(patsubst %,check-filter-%,$(FILTER_TEST_CLASSES) js processor): check-filter-%: test/t-istream-%
	exec $<

check-http-server: test/t-http-server-mirror
	./test/t-http-server.py

check-http-client: test/t-http-client test/t-http-server-mirror
	./test/t-http-client

check-cookie-client: test/run-cookie-client test/t-cookie-client
	python ./test/t-cookie-client.py
	./test/t-cookie-client

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

test/t-widget-registry: test/t-widget-registry.o src/widget-registry.o src/stock.o src/pool.o src/pstring.o src/uri-address.o src/tcache.o src/cache.o src/hashmap.o src/abort-unref.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

check-widget-registry: test/t-widget-registry
	./test/t-widget-registry

test/t-wembed: test/t-wembed.o src/wembed.o src/pool.o src/pstring.o src/widget-stream.o src/istream-delayed.o src/istream-hold.o src/istream-forward.o src/uri-parser.o src/uri-escape.o src/format.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBDAEMON_LIBS)

check-wembed: test/t-wembed
	./test/t-wembed

check: $(patsubst %,check-filter-%,$(FILTER_TEST_CLASSES) js processor) check-http-server check-http-client check-cookie-client check-shm check-dpool check-session check-widget-registry check-wembed

debug: src/cm4all-beng-proxy
	rm -f /tmp/cm4all-beng-proxy.gdb
	echo -en "handle SIGPIPE noprint nostop\nrun $(DEBUG_ARGS)\n" >/tmp/cm4all-beng-proxy.gdb
	LD_LIBRARY_PATH=/usr/lib/debug:$(LD_LIBRARY_PATH) gdb -x /tmp/cm4all-beng-proxy.gdb $<

profile: CFLAGS = -O3 -DNDEBUG -DSPLICE -DPROFILE -g -pg
profile: LDFLAGS = -lc_p -pg
profile: src/cm4all-beng-proxy
	./src/cm4all-beng-proxy -D -u max -p 8080

# -DNO_DATE_HEADER -DNO_XATTR -DNO_LAST_MODIFIED_HEADER
benchmark: CFLAGS = -O3 -DNDEBUG -DALWAYS_INLINE -DNO_ACCESS_LOG
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
