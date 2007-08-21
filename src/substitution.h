/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SUBSTITUTION_H
#define __BENG_SUBSTITUTION_H

#include "url-stream.h"
#include "processor.h"
#include "fifo-buffer.h"

#include <sys/types.h>

struct substitution;

struct substitution_handler {
    size_t (*output)(struct substitution *s, const void *data, size_t length);
    void (*eof)(struct substitution *s);
};

struct substitution {
    struct substitution *next;
    off_t start, end;
    const char *url;

    pool_t pool;

    url_stream_t url_stream;
    istream_t istream;
    int istream_eof;

    const struct substitution_handler *handler;
    void *handler_ctx;
};

void
substitution_start(struct substitution *s);

void
substitution_close(struct substitution *s);

void
substitution_output(struct substitution *s);

#endif
