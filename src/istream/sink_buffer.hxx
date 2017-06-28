#ifndef BENG_PROXY_SINK_BUFFER_HXX
#define BENG_PROXY_SINK_BUFFER_HXX

#include <exception>

#include <stddef.h>

struct pool;
class Istream;
class CancellablePointer;

struct sink_buffer_handler {
    void (*done)(void *data, size_t length, void *ctx);
    void (*error)(std::exception_ptr ep, void *ctx);
};

void
sink_buffer_new(struct pool &pool, Istream &input,
                const struct sink_buffer_handler &handler, void *ctx,
                CancellablePointer &cancel_ptr);

#endif
