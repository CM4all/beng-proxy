/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_catch.hxx"
#include "ForwardIstream.hxx"

#include <assert.h>

class CatchIstream final : public ForwardIstream {
    off_t available = 0;

    GError *(*const callback)(GError *error, void *ctx);
    void *const callback_ctx;

public:
    CatchIstream(struct pool &_pool, Istream &_input,
                 GError *(*_callback)(GError *error, void *ctx), void *ctx)
        :ForwardIstream(_pool, _input),
         callback(_callback), callback_ctx(ctx) {}

    void SendSpace();

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;

    off_t _Skip(off_t length) override {
        off_t nbytes = ForwardIstream::_Skip(length);
        if (nbytes > 0) {
            if (nbytes < available)
                available -= nbytes;
            else
                available = 0;
        }

        return nbytes;
    }

    void _Read() override;
    void _Close() override;

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnError(GError *error) override;
};

static constexpr char space[] =
    "                                "
    "                                "
    "                                "
    "                                ";

void
CatchIstream::SendSpace()
{
    assert(!HasInput());
    assert(available > 0);

    do {
        size_t length;
        if (available >= (off_t)sizeof(space) - 1)
            length = sizeof(space) - 1;
        else
            length = (size_t)available;

        size_t nbytes = ForwardIstream::OnData(space, length);
        if (nbytes == 0)
            return;

        available -= nbytes;
        if (nbytes < length)
            return;
    } while (available > 0);

    DestroyEof();
}


/*
 * istream handler
 *
 */

size_t
CatchIstream::OnData(const void *data, size_t length)
{
    if ((off_t)length > available)
        available = length;

    size_t nbytes = ForwardIstream::OnData(data, length);
    if (nbytes > 0) {
        if ((off_t)nbytes < available)
            available -= (off_t)nbytes;
        else
            available = 0;
    }

    return nbytes;
}

ssize_t
CatchIstream::OnDirect(FdType type, int fd, size_t max_length)
{
    ssize_t nbytes = ForwardIstream::OnDirect(type, fd, max_length);
    if (nbytes > 0) {
        if ((off_t)nbytes < available)
            available -= (off_t)nbytes;
        else
            available = 0;
    }

    return nbytes;
}

void
CatchIstream::OnError(GError *error)
{
    error = callback(error, callback_ctx);
    if (error != nullptr) {
        /* forward error to our handler */
        ForwardIstream::OnError(error);
        return;
    }

    /* the error has been handled by the callback, and he has disposed
       it */

    ClearInput();

    if (available > 0)
        /* according to a previous call to method "available", there
           is more data which we must provide - fill that with space
           characters */
        SendSpace();
    else
        DestroyEof();
}

/*
 * istream implementation
 *
 */

off_t
CatchIstream::_GetAvailable(bool partial)
{
    if (HasInput()) {
        off_t result = ForwardIstream::_GetAvailable(partial);
        if (result > available)
            available = result;

        return result;
    } else
        return available;
}

void
CatchIstream::_Read()
{
    if (HasInput())
        ForwardIstream::_Read();
    else
        SendSpace();
}

void
CatchIstream::_Close()
{
    if (HasInput())
        input.Close();

    Destroy();
}


/*
 * constructor
 *
 */

Istream *
istream_catch_new(struct pool *pool, Istream &input,
                  GError *(*callback)(GError *error, void *ctx), void *ctx)
{
    assert(callback != nullptr);

    return NewIstream<CatchIstream>(*pool, input, callback, ctx);
}
