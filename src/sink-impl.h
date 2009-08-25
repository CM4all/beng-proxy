#ifndef SINK_IMPL_H
#define SINK_IMPL_H

#include "istream.h"

struct async_operation_ref;

void
sink_null_new(istream_t istream);

void
sink_buffer_new(pool_t pool, istream_t input,
                void (*callback)(void *data, size_t length, void *ctx),
                void *ctx,
                struct async_operation_ref *async_ref);

void
sink_header_new(pool_t pool, istream_t input,
                void (*callback)(void *header, size_t length,
                                 istream_t tail, void *ctx),
                void *ctx,
                struct async_operation_ref *async_ref);

#endif
