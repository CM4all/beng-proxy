#ifndef BENG_PROXY_SINK_BUFFER_H
#define BENG_PROXY_SINK_BUFFER_H

#include "istream.h"

struct async_operation_ref;

struct sink_buffer_handler {
    void (*done)(void *data, size_t length, void *ctx);
    void (*error)(GError *error, void *ctx);
};

void
sink_buffer_new(pool_t pool, istream_t input,
                const struct sink_buffer_handler *handler, void *ctx,
                struct async_operation_ref *async_ref);

#endif
