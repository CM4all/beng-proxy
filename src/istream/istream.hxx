/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_HXX
#define ISTREAM_HXX

#include "Handler.hxx"
#include "istream_oo.hxx"
#include "pool.h"
#include "FdType.hxx"

#include <inline/compiler.h>

#include <assert.h>

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

    off_t available = Istream::Cast(*istream).GetAvailable(partial);

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

    off_t nbytes = Istream::Cast(*istream).Skip(length);
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

    Istream::Cast(*istream).Read();

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

    struct pool_notify_state notify;
    pool_notify(istream->pool, &notify);
    istream->reading = true;
#endif

    int fd = Istream::Cast(*istream).AsFd();

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

    Istream::Cast(*istream).Close();
}

static inline void
istream_free(struct istream **istream_r)
{
    struct istream *istream = *istream_r;
    *istream_r = nullptr;
    istream_close(istream);
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
