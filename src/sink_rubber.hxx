/*
 * An istream sink that copies data into a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_RUBBER_HXX
#define BENG_PROXY_SINK_RUBBER_HXX

#include <exception>

#include <stddef.h>

struct pool;
class Istream;
class Rubber;
class CancellablePointer;

class RubberSinkHandler {
public:
    virtual void RubberDone(unsigned rubber_id, size_t size) = 0;
    virtual void RubberOutOfMemory() = 0;
    virtual void RubberTooLarge() = 0;
    virtual void RubberError(std::exception_ptr ep) = 0;
};

void
sink_rubber_new(struct pool &pool, Istream &input,
                Rubber &rubber, size_t max_size,
                RubberSinkHandler &handler,
                CancellablePointer &cancel_ptr);

#endif
