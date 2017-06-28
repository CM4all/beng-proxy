/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_catch.hxx"
#include "ForwardIstream.hxx"
#include "GException.hxx"

#include <memory>

#include <glib.h>

#include <assert.h>

class CatchIstream final : public ForwardIstream {
    /**
     * This much data was announced by our input, either by
     * GetAvailable(), OnData() or OnDirect().
     */
    off_t available = 0;

    /**
     * The amount of data passed to OnData(), minus the number of
     * bytes consumed by it.  The next call must be at least this big.
     */
    size_t chunk = 0;

    std::exception_ptr (*const callback)(std::exception_ptr ep, void *ctx);
    void *const callback_ctx;

public:
    CatchIstream(struct pool &_pool, Istream &_input,
                 std::exception_ptr (*_callback)(std::exception_ptr ep, void *ctx), void *ctx)
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

            if ((size_t)nbytes < chunk)
                chunk -= nbytes;
            else
                chunk = 0;
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
    assert((off_t)chunk <= available);

    if (chunk > sizeof(space) - 1) {
        std::unique_ptr<char[]> buffer(new char[chunk]);
        std::fill_n(buffer.get(), ' ', chunk);
        size_t nbytes = ForwardIstream::OnData(buffer.get(), chunk);
        if (nbytes == 0)
            return;

        chunk -= nbytes;
        available -= nbytes;

        if (chunk > 0)
            return;

        if (available == 0) {
            DestroyEof();
            return;
        }
    }

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

    if (length > chunk)
        chunk = length;

    size_t nbytes = ForwardIstream::OnData(data, length);
    if (nbytes > 0) {
        if ((off_t)nbytes < available)
            available -= (off_t)nbytes;
        else
            available = 0;

        chunk -= nbytes;
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

        if ((size_t)nbytes < chunk)
            chunk -= nbytes;
        else
            chunk = 0;
    }

    return nbytes;
}

void
CatchIstream::OnError(GError *error)
{
    auto ep = callback(ToException(*error), callback_ctx);
    g_error_free(error);
    if (ep) {
        /* forward error to our handler */
        ForwardIstream::OnError(ToGError(ep));
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
                  std::exception_ptr (*callback)(std::exception_ptr ep, void *ctx), void *ctx)
{
    assert(callback != nullptr);

    return NewIstream<CatchIstream>(*pool, input, callback, ctx);
}
