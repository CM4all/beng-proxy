/*
 * An istream sink that copies data into a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_RUBBER_HXX
#define BENG_PROXY_SINK_RUBBER_HXX

#include "glibfwd.hxx"

#include <stddef.h>

struct pool;
struct istream;
class Rubber;
struct async_operation_ref;

struct sink_rubber_handler {
    void (*done)(unsigned rubber_id, size_t size, void *ctx);
    void (*out_of_memory)(void *ctx);
    void (*too_large)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

void
sink_rubber_new(struct pool *pool, struct istream *input,
                Rubber *rubber, size_t max_size,
                const struct sink_rubber_handler *handler, void *ctx,
                struct async_operation_ref *async_ref);

#endif
