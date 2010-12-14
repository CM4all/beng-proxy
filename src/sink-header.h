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

struct async_operation_ref;

void
sink_header_new(pool_t pool, istream_t input,
                void (*callback)(void *header, size_t length,
                                 istream_t tail, void *ctx),
                void *ctx,
                struct async_operation_ref *async_ref);

#endif
