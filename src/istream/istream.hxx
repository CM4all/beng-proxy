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
    assert(istream != nullptr);

    return Istream::Cast(*istream).GetAvailable(partial);
}

static inline off_t
istream_skip(struct istream *istream, off_t length)
{
    assert(istream != nullptr);

    return Istream::Cast(*istream).Skip(length);
}

static inline void
istream_read(struct istream *istream)
{
    assert(istream != nullptr);

    Istream::Cast(*istream).Read();
}

gcc_pure
static inline int
istream_as_fd(struct istream *istream)
{
    assert(istream != nullptr);

    return Istream::Cast(*istream).AsFd();
}

static inline void
istream_close(struct istream *istream)
{
    assert(istream != nullptr);

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
    assert(pool_contains(&istream->pool, istream, sizeof(*istream)));
    assert(handler != nullptr);
    assert(handler->data != nullptr);
    assert(handler->eof != nullptr);
    assert(handler->abort != nullptr);

    istream->handler = handler;
    istream->handler_ctx = handler_ctx;
    istream->handler_direct = handler_direct;
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
