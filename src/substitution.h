/*
 * Fill substitutions in a HTML stream, called by processor.c.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SUBSTITUTION_H
#define __BENG_SUBSTITUTION_H

#include "http-client.h"
#include "client-socket.h"
#include "processor.h"
#include "fifo-buffer.h"

#include <sys/types.h>

struct substitution;

struct substitution_handler {
    void (*output)(struct substitution *s);
};

typedef size_t (*substitution_output_t)(const void *data, size_t length, void *ctx);

struct substitution {
    struct substitution *next;
    off_t start, end;
    const char *url, *uri;

    pool_t pool;

    client_socket_t client_socket;
    http_client_connection_t http;
    istream_t istream;
    int istream_eof;
    fifo_buffer_t buffer;

    const struct substitution_handler *handler;
    void *handler_ctx;
};

void
substitution_start(struct substitution *s);

void
substitution_close(struct substitution *s);

size_t
substitution_output(struct substitution *s,
                    substitution_output_t callback, void *callback_ctx);

int
substitution_finished(const struct substitution *s);

#endif
