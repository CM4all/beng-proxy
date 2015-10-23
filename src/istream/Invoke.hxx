/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_INVOKE_HXX
#define ISTREAM_INVOKE_HXX

inline size_t
Istream::InvokeData(const void *data, size_t length)
{
    assert(!destroyed);
    assert(handler != nullptr);
    assert(handler->data != nullptr);
    assert(data != nullptr);
    assert(length > 0);
    assert(!in_data);
    assert(!eof);
    assert(!closing);
    assert(length >= data_available);
    assert(!available_full_set ||
           (off_t)length <= available_full);

#ifndef NDEBUG
    struct pool_notify_state notify;
    pool_notify(&pool, &notify);
    in_data = true;
#endif

    size_t nbytes = handler->data(data, length, handler_ctx);
    assert(nbytes <= length);
    assert(nbytes == 0 || !eof);

#ifndef NDEBUG
    if (pool_denotify(&notify) || destroyed) {
        assert(nbytes == 0);
        return nbytes;
    }

    in_data = false;
    data_available = length - nbytes;

    if (nbytes > 0)
        Consumed(nbytes);
#endif

    return nbytes;
}

inline ssize_t
Istream::InvokeDirect(FdType type, int fd, size_t max_length)
{
    assert(!destroyed);
    assert(handler != nullptr);
    assert(handler->direct != nullptr);
    assert((handler_direct & type) == type);
    assert(fd >= 0);
    assert(max_length > 0);
    assert(!in_data);
    assert(!eof);
    assert(!closing);

#ifndef NDEBUG
    struct pool_notify_state notify;
    pool_notify(&pool, &notify);
    in_data = true;
#endif

    ssize_t nbytes = handler->direct(type, fd, max_length, handler_ctx);
    assert(nbytes >= -3);
    assert(nbytes < 0 || (size_t)nbytes <= max_length);
    assert(nbytes == ISTREAM_RESULT_CLOSED || !eof);

#ifndef NDEBUG
    if (pool_denotify(&notify) || destroyed) {
        assert(nbytes == ISTREAM_RESULT_CLOSED);
        return nbytes;
    }

    assert(nbytes != ISTREAM_RESULT_CLOSED);

    in_data = false;

    if (nbytes > 0)
        Consumed(nbytes);
#endif

    return nbytes;
}

inline void
Istream::InvokeEof()
{
    assert(!destroyed);
    assert(!eof);
    assert(!closing);
    assert(data_available == 0);
    assert(available_partial == 0);
    assert(!available_full_set || available_full == 0);
    assert(handler != nullptr);

#ifndef NDEBUG
    eof = true;
#endif

    handler->eof(handler_ctx);
}

inline void
Istream::InvokeError(GError *error)
{
    assert(!destroyed);
    assert(!eof);
    assert(!closing);
    assert(handler != nullptr);
    assert(error != nullptr);

#ifndef NDEBUG
    eof = false;
#endif

    handler->abort(error, handler_ctx);
}

#endif
