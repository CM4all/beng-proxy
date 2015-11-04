#ifndef BENG_PROXY_SINK_BUFFER_HXX
#define BENG_PROXY_SINK_BUFFER_HXX

#include "glibfwd.hxx"

#include <stddef.h>

struct pool;
class Istream;
struct async_operation_ref;

struct sink_buffer_handler {
    void (*done)(void *data, size_t length, void *ctx);
    void (*error)(GError *error, void *ctx);
};

void
sink_buffer_new(struct pool &pool, Istream &input,
                const struct sink_buffer_handler &handler, void *ctx,
                struct async_operation_ref &async_ref);

#endif
