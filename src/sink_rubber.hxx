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
class Istream;
class Rubber;
struct async_operation_ref;

struct RubberSinkHandler {
    void (*done)(unsigned rubber_id, size_t size, void *ctx);
    void (*out_of_memory)(void *ctx);
    void (*too_large)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

void
sink_rubber_new(struct pool &pool, Istream &input,
                Rubber &rubber, size_t max_size,
                const RubberSinkHandler &handler, void *ctx,
                struct async_operation_ref &async_ref);

#endif
