/*
 * This istream filter reads a 32 bit header size from the stream,
 * reads it into a buffer and invokes a callback with the tail of the
 * stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_HEADER_H
#define BENG_PROXY_SINK_HEADER_H

#include "istream.h"

struct pool;
struct async_operation_ref;

struct sink_header_handler {
    void (*done)(void *header, size_t length, istream_t tail, void *ctx);
    void (*error)(GError *error, void *ctx);
};

void
sink_header_new(struct pool *pool, istream_t input,
                const struct sink_header_handler *handler, void *ctx,
                struct async_operation_ref *async_ref);

#endif
