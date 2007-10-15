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
    int reading, eof, in_data;
#endif

    /** try to read from the stream */
    void (*read)(istream_t istream);

    /** close the stream and free resources */
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

static inline void
istream_read(istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);
#ifndef NDEBUG
    pool_t pool = istream->pool;

    assert(istream->reading == 0);
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

    istream->close(_istream);
}

static inline void
istream_free(istream_t *istream_r)
{
    istream_t istream = *istream_r;
    *istream_r = NULL;
    istream_close(istream);
}

static inline void
istream_free_unref(istream_t *istream_r)
{
    istream_t istream = *istream_r;
    pool_t pool = istream_pool(istream);
    *istream_r = NULL;
    istream_close(istream);
    pool_unref(pool);
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

    istream->handler = NULL;
}

static inline void
istream_assign_ref(istream_t *istream_r, istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    *istream_r = _istream;
    pool_ref(istream->pool);
}

static inline void
istream_assign_ref_handler(istream_t *istream_r, istream_t istream,
                           const struct istream_handler *handler,
                           void *handler_ctx,
                           istream_direct_t handler_direct)
{
    istream_assign_ref(istream_r, istream);
    istream_handler_set(istream, handler, handler_ctx, handler_direct);
}

static inline void
istream_clear_unref(istream_t *istream_r)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    pool_unref(istream->pool);
}

static inline void
istream_clear_unref_handler(istream_t *istream_r)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    istream_handler_clear(istream_struct_cast(istream));
    pool_unref(istream->pool);
}


static inline size_t
istream_invoke_data(struct istream *istream, const void *data, size_t length)
{
    size_t nbytes;

    assert(istream != NULL);
    assert(istream->handler != NULL);
    assert(istream->handler->data != NULL);
    assert(data != NULL);
    assert(length > 0);
    assert(!istream->in_data);

#ifndef NDEBUG
    istream->in_data = 1;
#endif

    nbytes = istream->handler->data(data, length, istream->handler_ctx);
    assert(nbytes <= length);

#ifndef NDEBUG
    istream->in_data = 0;
#endif

    return nbytes;
}

static inline ssize_t
istream_invoke_direct(struct istream *istream, istream_direct_t type, int fd,
                      size_t max_length)
{
    ssize_t nbytes;

    assert(istream != NULL);
    assert(istream->handler != NULL);
    assert(istream->handler->direct != NULL);
    assert((istream->handler_direct & type) != 0);
    assert(fd >= 0);
    assert(max_length > 0);
    assert(!istream->in_data);

#ifndef NDEBUG
    istream->in_data = 1;
#endif

    nbytes = istream->handler->direct(type, fd, max_length, istream->handler_ctx);
    assert(nbytes < 0 || (size_t)nbytes <= max_length);

#ifndef NDEBUG
    istream->in_data = 0;
#endif

    return nbytes;
}

static inline void
istream_invoke_eof(struct istream *istream)
{
    assert(istream != NULL);
    assert(istream->handler != NULL);
    assert(!istream->eof);

#ifndef NDEBUG
    istream->eof = 1;
#endif

    if (istream->handler->eof != NULL)
        istream->handler->eof(istream->handler_ctx);
}

static inline void
istream_invoke_free(struct istream *istream)
{
    assert(istream != NULL);

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

istream_t
istream_deflate_new(pool_t pool, istream_t input);

istream_t
istream_subst_new(pool_t pool, istream_t input,
                  const char *a, const char *b);

#endif
