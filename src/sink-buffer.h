#ifndef BENG_PROXY_SINK_BUFFER_H
#define BENG_PROXY_SINK_BUFFER_H

#include "istream.h"

struct async_operation_ref;

void
sink_buffer_new(pool_t pool, istream_t input,
                void (*callback)(void *data, size_t length, void *ctx),
                void *ctx,
                struct async_operation_ref *async_ref);

#endif
