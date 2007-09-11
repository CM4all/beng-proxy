include version.mk

CC = gcc
CFLAGS = -O0 -g -DPOISON -DDEBUG_POOL_REF -DSPLICE
LDFLAGS =

ifeq ($(PROFILE),1)
CFLAGS += -UPOISON -UDEBUG_POOL_REF -DNDEBUG -DPROFILE -pg
LDFLAGS += -lc_p -pg
endif

WARNING_CFLAGS += -Wall -W -pedantic -Werror -pedantic-errors -std=gnu99 -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wfloat-equal -Wshadow -Wpointer-arith -Wbad-function-cast -Wsign-compare -Waggregate-return -Wmissing-declarations -Wmissing-noreturn -Wmissing-format-attribute -Wredundant-decls -Wnested-externs -Winline -Wdisabled-optimization -Wno-long-long -Wstrict-prototypes -Wundef

MORE_CFLAGS = -DVERSION=\"$(VERSION)\"

ALL_CFLAGS = $(CFLAGS) $(MORE_CFLAGS) $(WARNING_CFLAGS) 

LIBDAEMON_CFLAGS := $(shell pkg-config --cflags libcm4all-daemon)
LIBDAEMON_LIBS := $(shell pkg-config --libs libcm4all-daemon)

LIBEVENT_CFLAGS =
LIBEVENT_LIBS = -L/usr/local/lib -levent

LIBATTR_CFLAGS =
LIBATTR_LIBS = -lattr

SOURCES = src/main.c \
	src/child.c \
	src/session.c \
	src/connection.c \
	src/handler.c \
	src/file-handler.c \
	src/proxy-handler.c \
	src/replace.c \
	src/processor.c \
	src/parser.c \
	src/embed.c \
	src/socket-util.c \
	src/listener.c \
	src/client-socket.c \
	src/buffered-io.c \
	src/header-parser.c \
	src/header-writer.c \
	src/http-body.c \
	src/http-server.c \
	src/http-client.c \
	src/url-stream.c \
	src/fifo-buffer.c \
	src/growing-buffer.c \
	src/istream-memory.c \
	src/istream-string.c \
	src/istream-file.c \
	src/istream-chunked.c \
	src/istream-dechunk.c \
	src/istream-cat.c \
	src/istream-pipe.c \
	src/istream-delayed.c \
	src/istream-hold.c \
	src/uri.c \
	src/args.c \
	src/gmtime.c \
	src/date.c \
	src/strutil.c \
	src/format.c \
	src/strmap.c \
	src/pstring.c \
	src/pool.c

HEADERS = $(wildcard src/*.h)

OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

.PHONY: all clean

all: src/beng-proxy

clean:
	rm -f src/beng-proxy src/*.o doc/beng.{log,aux,ps,pdf,html} vgcore* core* gmon.out

src/beng-proxy: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS) $(LIBATTR_LIBS)

$(OBJECTS): %.o: %.c $(HEADERS)
	$(CC) -c -o $@ $< $(ALL_CFLAGS) $(WARNING_CFLAGS) $(LIBEVENT_CFLAGS) $(LIBDAEMON_CFLAGS) $(LIBATTR_CFLAGS)

profile: CFLAGS = -O0 -DNDEBUG -DSPLICE -DPROFILE -g -pg
profile: LDFLAGS = -lc_p -pg
profile: src/beng-proxy
	./src/beng-proxy

benchmark: CFLAGS = -O3 -DNDEBUG -DALWAYS_INLINE
benchmark: src/beng-proxy
	./src/beng-proxy

valgrind: CFLAGS = -O0 -g -DPOISON -DVALGRIND
valgrind: src/beng-proxy
	valgrind --show-reachable=yes --leak-check=yes ./src/beng-proxy

doc/beng.pdf: doc/beng.tex
	cd $(dir $<) && pdflatex $(notdir $<)

doc/beng.dvi: doc/beng.tex
	cd $(dir $<) && latex $(notdir $<)
