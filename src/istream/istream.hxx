/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_HXX
#define ISTREAM_HXX

#include "istream_class.hxx"
#include "pool.h"
#include "FdType.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <sys/types.h>

/**
 * These special values may be returned from
 * istream_handler::direct().
 */
enum istream_result {
    /**
     * No more data available in the specified socket.
     */
    ISTREAM_RESULT_EOF = 0,

    /**
     * I/O error, errno set.
     */
    ISTREAM_RESULT_ERRNO = -1,

    /**
     * Writing would block, callee is responsible for registering an
     * event and calling istream_read().
     */
    ISTREAM_RESULT_BLOCKING = -2,

    /**
     * The stream has ben closed.  This state supersedes all other
     * states.
     */
    ISTREAM_RESULT_CLOSED = -3,
};

/** data sink for an istream */
struct istream_handler {
    /**
     * Data is available as a buffer.
     * This function must return 0 if it has closed the stream.
     *
     * @param data the buffer
     * @param length the number of bytes available in the buffer, greater than 0
     * @param ctx the istream_handler context pointer
     * @return the number of bytes consumed, 0 if writing would block
     * (caller is responsible for registering an event) or if the
     * stream has been closed
     */
    size_t (*data)(const void *data, size_t length, void *ctx);

    /**
     * Data is available in a file descriptor.
     * This function must return 0 if it has closed the stream.
     *
     * @param type what kind of file descriptor?
     * @param fd the file descriptor
     * @param max_length don't read more than this number of bytes
     * @param ctx the istream_handler context pointer
     * @return the number of bytes consumed, or one of the
     * #istream_result values
     */
    ssize_t (*direct)(FdType type, int fd, size_t max_length,
                      void *ctx);

    /**
     * End of file encountered.
     *
     * @param ctx the istream_handler context pointer
     */
    void (*eof)(void *ctx);

    /**
     * The istream has ended unexpectedly, e.g. an I/O error.
     *
     * The method close() will not result in a call to this callback,
     * since the caller is assumed to be the istream handler.
     *
     * @param error a GError describing the error condition, must be
     * freed by the callee
     * @param ctx the istream_handler context pointer
     */
    void (*abort)(GError *error, void *ctx);
};

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

    const struct istream_class *cls;

    /** data sink */
    const struct istream_handler *handler;

    /** context pointer for the handler */
    void *handler_ctx;

    /** which types of file descriptors are accepted by the handler? */
    FdTypeMask handler_direct;

#ifndef NDEBUG
    bool reading, destroyed;

    bool closing:1, eof:1, in_data:1, available_full_set:1;

    /** how much data was available in the previous invocation? */
    size_t data_available;

    off_t available_partial, available_full;
#endif

    istream(struct pool &pool, const struct istream_class &cls);

    istream(const struct istream &) = delete;
    const istream &operator=(const struct istream &) = delete;
};

gcc_pure
static inline off_t
istream_available(struct istream *istream, bool partial)
{
#ifndef NDEBUG
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);

    struct pool_notify_state notify;
    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    off_t available;
    if (istream->cls->available == nullptr)
        available = (off_t)-1;
    else
        available = istream->cls->available(istream, partial);

#ifndef NDEBUG
    assert(available >= -1);
    assert(!pool_denotify(&notify));
    assert(!istream->destroyed);
    assert(istream->reading);

    istream->reading = false;

    if (partial) {
        assert(istream->available_partial == 0 ||
               available >= istream->available_partial);
        if (available > istream->available_partial)
            istream->available_partial = available;
    } else {
        assert(!istream->available_full_set ||
               istream->available_full == available);
        if (!istream->available_full_set && available != (off_t)-1) {
            istream->available_full = available;
            istream->available_full_set = true;
        }
    }
#endif

    return available;
}

static inline off_t
istream_skip(struct istream *istream, off_t length)
{
    off_t nbytes;
#ifndef NDEBUG
    struct pool_notify_state notify;

    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);

    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    if (istream->cls->skip == nullptr)
        nbytes = (off_t)-1;
    else
        nbytes = istream->cls->skip(istream, length);

    assert(nbytes <= length);

#ifndef NDEBUG
    if (pool_denotify(&notify) || istream->destroyed)
        return nbytes;

    istream->reading = false;

    if (nbytes > 0) {
        if (nbytes > istream->available_partial)
            istream->available_partial = 0;
        else
            istream->available_partial -= nbytes;

        assert(!istream->available_full_set ||
               nbytes < istream->available_full);
        if (istream->available_full_set)
            istream->available_full -= nbytes;
    }
#endif

    return nbytes;
}

static inline void
istream_read(struct istream *istream)
{
#ifndef NDEBUG
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);
    assert(!istream->in_data);

    struct pool_notify_state notify;
    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    istream->cls->read(istream);

#ifndef NDEBUG
    if (pool_denotify(&notify) || istream->destroyed)
        return;

    istream->reading = false;
#endif
}

gcc_pure
static inline int
istream_as_fd(struct istream *istream)
{
#ifndef NDEBUG
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);
    assert(!istream->in_data);
#endif

    if (istream->cls->as_fd == nullptr)
        return -1;

#ifndef NDEBUG
    struct pool_notify_state notify;
    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    int fd = istream->cls->as_fd(istream);

#ifndef NDEBUG
    assert(!pool_denotify(&notify) || fd < 0);

    if (fd < 0)
        istream->reading = false;
#endif

    return fd;
}

static inline void
istream_close(struct istream *istream)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(!istream->closing);
    assert(!istream->eof);

#ifndef NDEBUG
    istream->closing = true;
#endif

    istream->cls->close(istream);
}

static inline void
istream_free(struct istream **istream_r)
{
    struct istream *istream = *istream_r;
    *istream_r = nullptr;
    istream_close(istream);
}

gcc_pure
static inline bool
istream_has_handler(const struct istream *istream)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);

    return istream->handler != nullptr;
}


static inline void
istream_handler_set(struct istream *istream,
                    const struct istream_handler *handler,
                    void *handler_ctx,
                    FdTypeMask handler_direct)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(pool_contains(istream->pool, istream, sizeof(*istream)));
    assert(handler != nullptr);
    assert(handler->data != nullptr);
    assert(handler->eof != nullptr);
    assert(handler->abort != nullptr);

    istream->handler = handler;
    istream->handler_ctx = handler_ctx;
    istream->handler_direct = handler_direct;
}

static inline void
istream_assign_handler(struct istream **istream_r, struct istream *istream,
                       const struct istream_handler *handler,
                       void *handler_ctx,
                       FdTypeMask handler_direct)
{
    assert(istream_r != nullptr);
    assert(istream != nullptr);
    assert(!istream->destroyed);

    *istream_r = istream;
    istream_handler_set(istream, handler, handler_ctx, handler_direct);
}

static inline void
istream_handler_set_direct(struct istream *istream,
                           FdTypeMask handler_direct)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);

    istream->handler_direct = handler_direct;
}

static inline void
istream_handler_clear(struct istream *istream)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(!istream->eof);
    assert(istream->handler != nullptr);

    istream->handler = nullptr;
}

static inline void
istream_close_handler(struct istream *istream)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(istream_has_handler(istream));

    istream_handler_clear(istream);
    istream_close(istream);
}

static inline void
istream_free_handler(struct istream **istream_r)
{
    assert(istream_r != nullptr);
    assert(*istream_r != nullptr);
    assert(istream_has_handler(*istream_r));

    istream_handler_clear(*istream_r);
    istream_free(istream_r);
}

/**
 * Close an istream which was never used, i.e. it does not have a
 * handler yet.
 */
static inline void
istream_close_unused(struct istream *istream)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);
    assert(!istream_has_handler(istream));

    istream_close(istream);
}

/**
 * Free an istream which was never used, i.e. it does not have a
 * handler yet.
 */
static inline void
istream_free_unused(struct istream **istream_r)
{
    assert(istream_r != nullptr);
    assert(*istream_r != nullptr);
    assert(!istream_has_handler(*istream_r));

    istream_free(istream_r);
}

#endif
