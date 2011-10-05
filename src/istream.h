/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_H
#define __BENG_ISTREAM_H

#include "pool.h"
#include "istream-direct.h"

#include <glib.h>

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct istream *istream_t;

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
     * @return the number of bytes consumed, 0 if there is no more
     * data in the provided file descriptor, -1 on unhandled error
     * (errno set), -2 if writing would block (callee is responsible
     * for registering an event), -3 if the stream has been closed
     */
    ssize_t (*direct)(istream_direct_t type, int fd, size_t max_length, void *ctx);

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

/** an input stream */
struct istream {
    /** the memory pool which allocated this object */
    struct pool *pool;

    /** data sink */
    const struct istream_handler *handler;

    /** context pointer for the handler */
    void *handler_ctx;

    /** which types of file descriptors are accepted by the handler? */
    istream_direct_t handler_direct;

#ifndef NDEBUG
    bool reading, destroyed;

    bool closing:1, eof:1, in_data:1, available_full_set:1;

    /** how much data was available in the previous invocation? */
    size_t data_available;

    off_t available_partial, available_full;
#endif

    /**
     * How much data is available? 
     *
     * @param partial if false, the stream must provide the data size
     * until the end of the stream; for partial, a minimum estimate is
     * ok
     * @return the number of bytes available or -1 if the object does
     * not know
     */
    off_t (*available)(struct istream *istream, bool partial);

    /**
     * Skip data without processing it.  By skipping 0 bytes, you can
     * test whether the stream is able to skip at all.
     *
     * @return the number of bytes skipped or -1 if skipping is not supported
     */
    off_t (*skip)(struct istream *istream, off_t length);

    /**
     * Try to read from the stream.  If the stream can read data
     * without blocking, it must provide data.  It may invoke the
     * callbacks any number of times, supposed that the handler itself
     * doesn't block.
     *
     * If the stream does not provide data immediately (and it is not
     * at EOF yet), it must install an event and invoke the handler
     * later, whenever data becomes available.
     *
     * Whenever the handler reports it is blocking, the responsibility
     * for calling back (and calling this function) is handed back to
     * the istream handler.
     */
    void (*read)(struct istream *istream);

    /**
     * Close the istream object, and return the remaining data as a
     * file descriptor.  This fd can be read until end-of-stream.
     * Returns -1 if this is not possible (the istream object is still
     * usable).
     */
    int (*as_fd)(struct istream *istream);

    /**
     * Close the stream and free resources.  This must not be called
     * after the handler's eof() / abort() callbacks were invoked.
     */
    void (*close)(struct istream *istream);
};

static inline struct istream *
istream_struct_cast(struct istream *istream)
{
    assert(istream != NULL);

    return (struct istream *)istream;
}

static inline off_t
istream_available(struct istream *istream, bool partial)
{
    off_t available;
#ifndef NDEBUG
    struct pool_notify notify;

    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);

    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    if (istream->available == NULL)
        available = (off_t)-1;
    else
        available = istream->available(istream, partial);

#ifndef NDEBUG
    assert(available >= -1);

    if (pool_denotify(&notify) || istream->destroyed)
        return available;

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
    struct pool_notify notify;

    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);

    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    if (istream->skip == NULL)
        nbytes = (off_t)-1;
    else
        nbytes = istream->skip(istream, length);

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
    struct pool_notify notify;

    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);
    assert(!istream->in_data);

    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    istream->read(istream);

#ifndef NDEBUG
    if (pool_denotify(&notify) || istream->destroyed)
        return;

    istream->reading = false;
#endif
}

static inline int
istream_as_fd(struct istream *istream)
{
#ifndef NDEBUG
    struct pool_notify notify;

    assert(!istream->closing);
    assert(!istream->eof);
    assert(!istream->reading);
    assert(!istream->in_data);
#endif

    if (istream->as_fd == NULL)
        return -1;

#ifndef NDEBUG
    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    int fd = istream->as_fd(istream);

#ifndef NDEBUG
    assert(!pool_denotify(&notify) || fd >= 0);

    if (fd < 0)
        istream->reading = false;
#endif

    return fd;
}

static inline void
istream_close(struct istream *istream)
{
    assert(!istream->closing);
    assert(!istream->eof);

#ifndef NDEBUG
    istream->closing = true;
#endif

    istream->close(istream);
}

static inline void
istream_free(struct istream **istream_r)
{
    struct istream *istream = *istream_r;
    *istream_r = NULL;
    istream_close(istream);
}

static inline bool
istream_has_handler(struct istream *istream)
{
    return istream->handler != NULL;
}


static inline void
istream_handler_set(struct istream *istream,
                    const struct istream_handler *handler,
                    void *handler_ctx,
                    istream_direct_t handler_direct)
{
    assert(pool_contains(istream->pool, istream, sizeof(*istream)));
    assert(handler != NULL);
    assert(handler->data != NULL);
    assert(handler->eof != NULL);
    assert(handler->abort != NULL);

    istream->handler = handler;
    istream->handler_ctx = handler_ctx;
    istream->handler_direct = handler_direct;
}

static inline void
istream_assign_handler(struct istream **istream_r, struct istream *istream,
                       const struct istream_handler *handler,
                       void *handler_ctx,
                       istream_direct_t handler_direct)
{
    *istream_r = istream;
    istream_handler_set(istream, handler, handler_ctx, handler_direct);
}

static inline void
istream_handler_set_direct(struct istream *istream,
                           istream_direct_t handler_direct)
{
    istream->handler_direct = handler_direct;
}

static inline void
istream_handler_clear(struct istream *istream)
{
    assert(!istream->eof);
    assert(istream->handler != NULL);

    istream->handler = NULL;
}

static inline void
istream_close_handler(struct istream *istream)
{
    assert(istream != NULL);
    assert(istream_has_handler(istream));

    istream_handler_clear(istream);
    istream_close(istream);
}

static inline void
istream_free_handler(struct istream **istream_r)
{
    assert(istream_r != NULL);
    assert(*istream_r != NULL);
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
    assert(istream_r != NULL);
    assert(*istream_r != NULL);
    assert(!istream_has_handler(*istream_r));

    istream_free(istream_r);
}

#include "istream-impl.h"

#endif
