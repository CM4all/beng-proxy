/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_STRUCT_HXX
#define ISTREAM_STRUCT_HXX

#include "FdType.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <sys/types.h>

struct pool;
struct istream_handler;

/**
 * An asynchronous input stream.
 *
 * The lifetime of an #istream begins when it is created, and ends
 * with one of the following events:
 *
 * - it is closed manually using istream_close()
 * - it is invalidated by a successful istream_as_fd() call
 * - it has reached end-of-file
 * - an error has occurred
 */
struct istream {
    /** the memory pool which allocated this object */
    struct pool *pool;

    /** data sink */
    const struct istream_handler *handler = nullptr;

    /** context pointer for the handler */
    void *handler_ctx;

    /** which types of file descriptors are accepted by the handler? */
    FdTypeMask handler_direct = 0;

#ifndef NDEBUG
    bool reading = false, destroyed = false;

    bool closing = false, eof = false;

    bool in_data = false, available_full_set = false;

    /** how much data was available in the previous invocation? */
    size_t data_available = 0;

    off_t available_partial = 0, available_full = 0;
#endif

    istream(struct pool &pool);
    ~istream();

    istream(const struct istream &) = delete;
    const istream &operator=(const struct istream &) = delete;
};

gcc_pure
static inline bool
istream_has_handler(const struct istream *istream)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);

    return istream->handler != nullptr;
}

#endif
