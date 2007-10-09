/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_H
#define __BENG_ISTREAM_H

#include "pool.h"

#ifndef NDEBUG
#include <assert.h>
#endif

#include <sys/types.h>

enum istream_direct {
    ISTREAM_FILE = 01,
    ISTREAM_PIPE = 02,
    ISTREAM_SOCKET = 04,
    ISTREAM_ANY = (ISTREAM_FILE | ISTREAM_PIPE | ISTREAM_SOCKET),
};

typedef unsigned istream_direct_t;

typedef struct istream *istream_t;

/** data sink for an istream */
struct istream_handler {
    /**
     * Data is available as a buffer.
     *
     * @param data the buffer
     * @param length the number of bytes available in the buffer, greater than 0
     * @param ctx the istream_handler context pointer
     * @return the number of bytes consumed
     */
    size_t (*data)(const void *data, size_t length, void *ctx);

    /**
     * Data is available in a file descriptor.
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
     * The istream is being disposed.
     *
     * @param ctx the istream_handler context pointer
     */
    void (*free)(void *ctx);
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
    int reading;
#endif

    /** try to read from the stream */
    void (*read)(istream_t istream);

    /** close the stream and free resources */
    void (*close)(istream_t istream);
};

static inline void
istream_read(istream_t istream)
{
#ifndef NDEBUG
    pool_t pool = istream->pool;

    assert(istream->reading == 0);
    pool_ref(pool);
    istream->reading = 1;
#endif

    istream->read(istream);

#ifndef NDEBUG
    istream->reading = 0;
    pool_unref(pool);
#endif
}

static inline void
istream_close(istream_t istream)
{
    istream->close(istream);
}

static inline void
istream_free(istream_t *istream_r)
{
    istream_t istream = *istream_r;
    *istream_r = NULL;
    istream_close(istream);
}


static inline void
istream_assign_ref(istream_t *istream_r, istream_t istream)
{
    *istream_r = istream;
    pool_ref(istream->pool);
}

static inline void
istream_assign_ref_handler(istream_t *istream_r, istream_t istream,
                           const struct istream_handler *handler,
                           void *handler_ctx,
                           istream_direct_t handler_direct)
{
    istream_assign_ref(istream_r, istream);
    istream->handler = handler;
    istream->handler_ctx = handler_ctx;
    istream->handler_direct = handler_direct;
}

static inline void
istream_clear_unref(istream_t *istream_r)
{
    istream_t istream = *istream_r;
    *istream_r = NULL;
    pool_unref(istream->pool);
}

static inline void
istream_clear_unref_handler(istream_t *istream_r)
{
    istream_t istream = *istream_r;
    *istream_r = NULL;
    istream->handler = NULL;
    pool_unref(istream->pool);
}


static inline size_t
istream_invoke_data(istream_t istream, const void *data, size_t length)
{
    return istream->handler->data(data, length, istream->handler_ctx);
}

static inline ssize_t
istream_invoke_direct(istream_t istream, istream_direct_t type, int fd, size_t max_length)
{
    return istream->handler->direct(type, fd, max_length, istream->handler_ctx);
}

static inline void
istream_invoke_eof(istream_t istream)
{
    if (istream->handler->eof != NULL)
        istream->handler->eof(istream->handler_ctx);
}

static inline void
istream_invoke_free(istream_t istream)
{
    if (istream->handler != NULL && istream->handler->free != NULL) {
        const struct istream_handler *handler = istream->handler;
        void *handler_ctx = istream->handler_ctx;
        istream->handler = NULL;
        istream->handler_ctx = NULL;
        handler->free(handler_ctx);
    }
}

istream_t
istream_null_new(pool_t pool);

istream_t attr_malloc
istream_memory_new(pool_t pool, const void *data, size_t length);

istream_t attr_malloc
istream_string_new(pool_t pool, const char *s);

istream_t attr_malloc
istream_file_new(pool_t pool, const char *path, off_t length);

#ifdef __linux
istream_t
istream_pipe_new(pool_t pool, istream_t input);
#endif

istream_t
istream_chunked_new(pool_t pool, istream_t input);

istream_t
istream_dechunk_new(pool_t pool, istream_t input,
                    void (*eof_callback)(void *ctx), void *ctx);

istream_t
istream_cat_new(pool_t pool, ...);

istream_t
istream_delayed_new(pool_t pool, void (*abort_callback)(void *ctx),
                    void *callback_ctx);

void
istream_delayed_set(istream_t istream_delayed, istream_t input);

istream_t
istream_hold_new(pool_t pool, istream_t input);

#endif
