/*
 * Asynchronous input stream API, utilities for istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_INVOKE_H
#define __BENG_ISTREAM_INVOKE_H

static inline size_t
istream_invoke_data(struct istream *istream, const void *data, size_t length)
{
#ifndef NDEBUG
    struct pool_notify notify;
#endif
    size_t nbytes;

    assert(istream != NULL);
    assert(istream->handler != NULL);
    assert(istream->handler->data != NULL);
    assert(data != NULL);
    assert(length > 0);
    assert(!istream->in_data);
    assert(!istream->eof);
    assert(length >= istream->data_available);
    assert(!istream->available_full_set ||
           (off_t)length <= istream->available_full);

#ifndef NDEBUG
    pool_notify(istream->pool, &notify);
    istream->in_data = 1;
#endif

    nbytes = istream->handler->data(data, length, istream->handler_ctx);
    assert(nbytes <= length);
    assert(nbytes == 0 || !istream->eof);

#ifndef NDEBUG
    if (pool_denotify(&notify)) {
        assert(nbytes == 0);
        return nbytes;
    }

    istream->in_data = 0;
    istream->data_available = length - nbytes;

    if (nbytes > 0) {
        if ((ssize_t)nbytes < 0 ||
            (off_t)nbytes >= istream->available_partial)
            istream->available_partial = 0;
        else 
            istream->available_partial -= nbytes;

        if (istream->available_full_set)
            istream->available_full -= (off_t)nbytes;
    }
#endif

    return nbytes;
}

static inline ssize_t
istream_invoke_direct(struct istream *istream, istream_direct_t type, int fd,
                      size_t max_length)
{
#ifndef NDEBUG
    struct pool_notify notify;
#endif
    ssize_t nbytes;

    assert(istream != NULL);
    assert(istream->handler != NULL);
    assert(istream->handler->direct != NULL);
    assert((istream->handler_direct & type) == type);
    assert(fd >= 0);
    assert(max_length > 0);
    assert(!istream->in_data);
    assert(!istream->eof);

#ifndef NDEBUG
    pool_notify(istream->pool, &notify);
    istream->in_data = 1;
#endif

    nbytes = istream->handler->direct(type, fd, max_length, istream->handler_ctx);
    assert(nbytes < 0 || (size_t)nbytes <= max_length);
    assert(nbytes == 0 || !istream->eof);

#ifndef NDEBUG
    if (pool_denotify(&notify)) {
        assert(nbytes == 0);
        return nbytes;
    }

    istream->in_data = 0;

    if (nbytes > 0) {
        if ((off_t)nbytes >= istream->available_partial)
            istream->available_partial = 0;
        else 
            istream->available_partial -= nbytes;

        assert(!istream->available_full_set ||
               (off_t)nbytes <= istream->available_full);
        if (istream->available_full_set)
            istream->available_full -= (off_t)nbytes;
    }
#endif

    return nbytes;
}

static inline void
istream_invoke_eof(struct istream *istream)
{
    assert(istream != NULL);
    assert(!istream->eof);
    assert(istream->available_partial == 0);
    assert(!istream->available_full_set || istream->available_full == 0);

#ifndef NDEBUG
    istream->eof = 1;
#endif

    if (istream->handler != NULL && istream->handler->eof != NULL)
        istream->handler->eof(istream->handler_ctx);
}

static inline void
istream_invoke_abort(struct istream *istream)
{
    assert(istream != NULL);
    assert(!istream->eof);

#ifndef NDEBUG
    istream->eof = 1;
#endif

    if (istream->handler != NULL && istream->handler->abort != NULL) {
        const struct istream_handler *handler = istream->handler;
        void *handler_ctx = istream->handler_ctx;
        istream->handler = NULL;
        istream->handler_ctx = NULL;
        handler->abort(handler_ctx);
    }
}

#endif
