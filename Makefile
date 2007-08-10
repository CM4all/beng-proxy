include version.mk

CC = gcc
CFLAGS = -O0 -g
override CFLAGS += -Wall -W -pedantic -Werror -pedantic-errors -std=gnu99 -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wfloat-equal -Wshadow -Wpointer-arith -Wbad-function-cast -Wsign-compare -Waggregate-return -Wmissing-declarations -Wmissing-noreturn -Wmissing-format-attribute -Wredundant-decls -Wnested-externs -Winline -Wdisabled-optimization -Wno-long-long -Wstrict-prototypes -Wundef

override CFLAGS += -DVERSION=\"$(VERSION)\"

LIBDAEMON_CFLAGS := $(shell pkg-config --cflags libcm4all-daemon)
LIBDAEMON_LIBS := $(shell pkg-config --libs libcm4all-daemon)

LIBEVENT_CFLAGS =
LIBEVENT_LIBS = -levent

SOURCES = src/main.c \
	src/handler.c \
	src/listener.c \
	src/http-server.c \
	src/fifo-buffer.c \
	src/strutil.c \
	src/strmap.c \
	src/pool.c

HEADERS = $(wildcard src/*.h)

OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

.PHONY: all clean

all: src/beng-proxy

clean:
	rm -f src/beng-proxy src/*.o doc/beng.{log,aux,ps,pdf,html}

src/beng-proxy: $(OBJECTS)
	$(CC) -o $@ $^ $(LIBEVENT_LIBS) $(LIBDAEMON_LIBS)

$(OBJECTS): %.o: %.c $(HEADERS)
	$(CC) -c $(CFLAGS) -o $@ $< $(LIBEVENT_CFLAGS) $(LIBDAEMON_CFLAGS)

doc/beng.pdf: doc/beng.tex
	cd $(dir $<) && pdflatex $(notdir $<)

doc/beng.dvi: doc/beng.tex
	cd $(dir $<) && latex $(notdir $<)
