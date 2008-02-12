/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_H
#define __BENG_ISTREAM_H

#include "pool.h"

#include <assert.h>
#include <sys/types.h>

enum istream_direct {
    ISTREAM_FILE = 01,
    ISTREAM_PIPE = 02,
    ISTREAM_SOCKET = 04,
    ISTREAM_ANY = (ISTREAM_FILE | ISTREAM_PIPE | ISTREAM_SOCKET),
};

typedef unsigned istream_direct_t;

typedef struct istream_opaque *istream_t;

/** data sink for an istream */
struct istream_handler {
    /**
     * Data is available as a buffer.
     * This function must return 0 if it has closed the stream.
     *
     * @param data the buffer
     * @param length the number of bytes available in the buffer, greater than 0
     * @param ctx the istream_handler context pointer
     * @return the number of bytes consumed
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
     * @return the number of bytes consumed, -1 on error (errno set),
     *   -2 if writing would block
     */
    ssize_t (*direct)(istream_direct_t type, int fd, size_t max_length, void *ctx);

    /**
     * End of file encountered.
     *
     * @param ctx the istream_handler context pointer
     */
    void (*eof)(void *ctx);

    /**
     * The istream has ended unexpectedly, e.g. an I/O error, or a
     * client called istream_close().
     *
     * @param ctx the istream_handler context pointer
     */
    void (*abort)(void *ctx);
};

/** an input stream */
struct istream {
    /** the memory pool which allocated this object */
    pool_t pool;

    /** data sink */
    const struct istream_handler *handler;

    /** context pointer for the handler */
    void *handler_ctx;

    /** which types of file descriptors are accepted by the handler? */
    istream_direct_t handler_direct;

#ifndef NDEBUG
    unsigned reading:1, eof:1, in_data:1;

    /** how much data was available in the previous invocation? */
    size_t data_available;

    off_t available_check;
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
    off_t (*available)(istream_t istream, int partial);

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
    void (*read)(istream_t istream);

    /**
     * Close the stream and free resources.  This must not be called
     * after the handler's eof() / abort() callbacks were invoked.
     */
    void (*close)(istream_t istream);
};

static inline struct istream *
_istream_opaque_cast(istream_t istream)
{
    assert(istream != NULL);

    return (struct istream *)istream;
}

static inline istream_t
istream_struct_cast(struct istream *istream)
{
    assert(istream != NULL);

    return (istream_t)istream;
}

static inline pool_t
istream_pool(istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    assert(istream->pool != NULL);

    return istream->pool;
}

static inline off_t
istream_available(istream_t _istream, int partial)
{
    struct istream *istream = _istream_opaque_cast(_istream);
    off_t available;
#ifndef NDEBUG
    pool_t pool = istream->pool;

    assert(!istream->eof);

    assert(istream->reading == 0);
    pool_ref(pool);
    istream->reading = 1;
#endif

    if (istream->available == NULL)
        available = (off_t)-1;
    else
        available = istream->available(_istream, partial);

#ifndef NDEBUG
    istream->reading = 0;
    pool_unref(pool);

    assert(partial || available == (off_t)-1 ||
           available >= istream->available_check);
    if (available > istream->available_check)
        istream->available_check = available;
#endif

    return available;
}

static inline void
istream_read(istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);
#ifndef NDEBUG
    pool_t pool = istream->pool;

    assert(!istream->eof);
    assert(!istream->reading);
    assert(!istream->in_data);

    pool_ref(pool);
    istream->reading = 1;
#endif

    istream->read(_istream);

#ifndef NDEBUG
    istream->reading = 0;
    pool_unref(pool);
#endif
}

static inline void
istream_close(istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    assert(!istream->eof);

    istream->close(_istream);
}

static inline void
istream_free(istream_t *istream_r)
{
    istream_t istream = *istream_r;
    *istream_r = NULL;
    istream_close(istream);
}

static inline int
istream_has_handler(istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    return istream->handler != NULL;
}


static inline void
istream_handler_set(istream_t _istream,
                    const struct istream_handler *handler,
                    void *handler_ctx,
                    istream_direct_t handler_direct)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    assert(pool_contains(istream->pool, istream, sizeof(*istream)));
    assert(handler != NULL);
    assert(handler->data != NULL);

    istream->handler = handler;
    istream->handler_ctx = handler_ctx;
    istream->handler_direct = handler_direct;
}

static inline void
istream_handler_set_direct(istream_t _istream,
                           istream_direct_t handler_direct)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    istream->handler_direct = handler_direct;
}

static inline void
istream_handler_clear(istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    assert(!istream->eof);

    istream->handler = NULL;
}


#include "istream-ref.h"
#include "istream-invoke.h"
#include "istream-impl.h"

#endif
