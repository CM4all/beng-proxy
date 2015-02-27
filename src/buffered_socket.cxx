/*
 * Wrapper for a socket file descriptor with input and output
 * buffer management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "buffered_socket.hxx"
#include "fb_pool.hxx"
#include "pool.hxx"
#include "gerrno.h"
#include "util/ConstBuffer.hxx"

#include <utility>

#include <limits.h>
#include <errno.h>

void
BufferedSocket::ClosedPrematurely()
{
    GError *error =
        g_error_new_literal(buffered_socket_quark(), 0,
                            "Peer closed the socket prematurely");
    handler->error(error, handler_ctx);
}

void
BufferedSocket::Ended()
{
    assert(!IsConnected());
    assert(!ended);

#ifndef NDEBUG
    ended = true;
#endif

    if (handler->end == nullptr)
        ClosedPrematurely();
    else
        handler->end(handler_ctx);
}

int
BufferedSocket::AsFD()
{
    if (!IsEmpty())
        /* can switch to the raw socket descriptor only if the input
           buffer is empty */
        return -1;

    return base.AsFD();
}

size_t
BufferedSocket::GetAvailable() const
{
    assert(!ended);

    return input.GetAvailable();
}

void
BufferedSocket::Consumed(size_t nbytes)
{
    assert(!ended);

    input.Consume(nbytes);
}

/**
 * Invokes the data handler, and takes care for
 * #BufferedResult::AGAIN_OPTIONAL and #BufferedResult::AGAIN_EXPECT.
 */
inline BufferedResult
BufferedSocket::InvokeData()
{
    assert(!IsEmpty());

    bool local_expect_more = false;

    while (true) {
        auto r = input.Read();
        if (r.IsEmpty())
            return expect_more || local_expect_more
                ? BufferedResult::MORE
                : BufferedResult::OK;

#ifndef NDEBUG
        PoolNotify notify(base.GetPool());
#endif

        BufferedResult result = handler->data(r.data, r.size, handler_ctx);

#ifndef NDEBUG
        if (notify.Denotify()) {
            assert(result == BufferedResult::CLOSED);
        } else {
            last_buffered_result = result;
            assert((result == BufferedResult::CLOSED) == !IsValid());
        }
#endif

        if (result == BufferedResult::AGAIN_EXPECT)
            local_expect_more = true;
        else if (result == BufferedResult::AGAIN_OPTIONAL)
            local_expect_more = false;
        else
            return result;
    }
}

bool
BufferedSocket::SubmitFromBuffer()
{
    if (IsEmpty())
        return true;

    const bool old_expect_more = expect_more;
    expect_more = false;

    BufferedResult result = InvokeData();
    assert((result == BufferedResult::CLOSED) || IsValid());

    switch (result) {
    case BufferedResult::OK:
        assert(input.IsEmpty());
        assert(!expect_more);

        input.Free(fb_pool_get());

        if (!IsConnected()) {
            Ended();
            return false;
        }

        if (!base.IsReadPending())
            /* try to refill the buffer, now that it's become empty
               (but don't refresh the pending timeout) */
            base.ScheduleRead(read_timeout);

        return true;

    case BufferedResult::PARTIAL:
        assert(!input.IsEmpty());

        if (!IsConnected())
            return false;

        return true;

    case BufferedResult::MORE:
        expect_more = true;

        if (!IsConnected()) {
            ClosedPrematurely();
            return false;
        }

        if (IsFull()) {
            GError *error =
                g_error_new_literal(buffered_socket_quark(), 0,
                                    "Input buffer overflow");
            handler->error(error, handler_ctx);
            return false;
        }

        return true;

    case BufferedResult::AGAIN_OPTIONAL:
    case BufferedResult::AGAIN_EXPECT:
        /* unreachable, has been handled by InvokeData() */
        assert(false);
        gcc_unreachable();

    case BufferedResult::BLOCKING:
        expect_more = old_expect_more;
        return false;

    case BufferedResult::CLOSED:
        /* the buffered_socket object has been destroyed by the
           handler */
        return false;
    }

    assert(false);
    gcc_unreachable();
}

/**
 * @return true if more data should be read from the socket
 */
inline bool
BufferedSocket::SubmitDirect()
{
    assert(IsConnected());
    assert(IsEmpty());

    const bool old_expect_more = expect_more;
    expect_more = false;

    const DirectResult result =
        handler->direct(base.GetFD(), base.GetType(), handler_ctx);
    switch (result) {
    case DirectResult::OK:
        /* some data was transferred: refresh the read timeout */
        base.ScheduleRead(read_timeout);
        return true;

    case DirectResult::BLOCKING:
        expect_more = old_expect_more;
        base.UnscheduleRead();
        defer_event_cancel(&defer_read);
        return false;

    case DirectResult::EMPTY:
        /* schedule read, but don't refresh timeout of old scheduled
           read */
        if (!base.IsReadPending())
            base.ScheduleRead(read_timeout);
        return true;

    case DirectResult::END:
        Ended();
        return false;

    case DirectResult::CLOSED:
        return false;

    case DirectResult::ERRNO:
        handler->error(new_error_errno(), handler_ctx);
        return false;
    }

    assert(false);
    gcc_unreachable();
}

inline bool
BufferedSocket::FillBuffer()
{
    assert(IsConnected());

    if (input.IsNull())
        input.Allocate(fb_pool_get());

    ssize_t nbytes = base.ReadToBuffer(input, INT_MAX);
    if (gcc_likely(nbytes > 0)) {
        /* success: data was added to the buffer */
        expect_more = false;
        got_data = true;

        return true;
    }

    if (nbytes == 0) {
        /* socket closed */

        if (expect_more) {
            ClosedPrematurely();
            return false;
        }

        assert(handler->closed != nullptr);

        const size_t remaining = input.GetAvailable();

        if (!handler->closed(handler_ctx) ||
            (handler->remaining != nullptr &&
             !handler->remaining(remaining, handler_ctx)))
            return false;

        assert(!IsConnected());
        assert(remaining == input.GetAvailable());

        if (input.IsEmpty()) {
            Ended();
            return false;
        }

        return true;
    }

    if (nbytes == -2) {
        /* input buffer is full */
        base.UnscheduleRead();
        defer_event_cancel(&defer_read);
        return true;
    }

    if (nbytes == -1) {
        if (errno == EAGAIN) {
            /* schedule read, but don't refresh timeout of old
               scheduled read */
            if (!base.IsReadPending())
                base.ScheduleRead(read_timeout);
            return true;
        } else {
            GError *error = new_error_errno_msg("recv() failed");
            handler->error(error, handler_ctx);
            return false;
        }
    }

    return true;
}

inline bool
BufferedSocket::TryRead2()
{
    assert(IsValid());
    assert(!destroyed);
    assert(!ended);
    assert(reading);

    if (!IsConnected()) {
        assert(!IsEmpty());

        SubmitFromBuffer();
        return false;
    } else if (direct) {
        /* empty the remaining buffer before doing direct transfer */
        if (!SubmitFromBuffer())
            return false;

        if (!direct)
            /* meanwhile, the "direct" flag was reverted by the
               handler - try again */
            return TryRead2();

        if (!IsEmpty()) {
            /* there's still data in the buffer, but our handler isn't
               ready for consuming it - stop reading from the
               socket */
            base.UnscheduleRead();
            defer_event_cancel(&defer_read);
            return true;
        }

        return SubmitDirect();
    } else {
        got_data = false;

        if (!FillBuffer())
            return false;

        if (!SubmitFromBuffer())
            return false;

        if (got_data)
            /* refresh the timeout each time data was received */
            base.ScheduleRead(read_timeout);
        return true;
    }
}

bool
BufferedSocket::TryRead()
{
    assert(IsValid());
    assert(!destroyed);
    assert(!ended);
    assert(!reading);

#ifndef NDEBUG
    PoolNotify notify(base.GetPool());
    reading = true;
#endif

    const bool result = TryRead2();

#ifndef NDEBUG
    if (!notify.Denotify()) {
        assert(reading);
        reading = false;
    }
#endif

    return result;
}

/*
 * socket_wrapper handler
 *
 */

bool
BufferedSocket::OnWrite(void *ctx)
{
    BufferedSocket *s = (BufferedSocket *)ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return s->handler->write(s->handler_ctx);
}

bool
BufferedSocket::OnRead(void *ctx)
{
    BufferedSocket *s = (BufferedSocket *)ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return s->TryRead();
}

bool
BufferedSocket::OnTimeout(void *ctx)
{
    BufferedSocket *s = (BufferedSocket *)ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    if (s->handler->timeout != nullptr)
        return s->handler->timeout(s->handler_ctx);

    s->handler->error(g_error_new_literal(buffered_socket_quark(), 0,
                                          "Timeout"),
                      s->handler_ctx);
    return false;
}

const struct socket_handler BufferedSocket::buffered_socket_handler = {
    .read = OnRead,
    .write = OnWrite,
    .timeout = OnTimeout,
};

/*
 * defer_event handler
 *
 */

static void
buffered_socket_defer_read(evutil_socket_t, short, void *ctx)
{
    BufferedSocket &s = *(BufferedSocket *)ctx;

    s.Read(false);
}

/*
 * public API
 *
 */

void
BufferedSocket::Init(struct pool &_pool,
                     int _fd, enum istream_direct _fd_type,
                     const struct timeval *_read_timeout,
                     const struct timeval *_write_timeout,
                     const BufferedSocketHandler &_handler, void *_ctx)
{
    assert(_handler.data != nullptr);
    /* handler method closed() is optional */
    assert(_handler.write != nullptr);
    assert(_handler.error != nullptr);

    base.Init(_pool, _fd, _fd_type,
              buffered_socket_handler, this);

    read_timeout = _read_timeout;
    write_timeout = _write_timeout;

    defer_event_init(&defer_read, buffered_socket_defer_read, this);

    handler = &_handler;
    handler_ctx = _ctx;
    input.SetNull();
    direct = false;
    expect_more = false;
    destroyed = false;

#ifndef NDEBUG
    reading = false;
    ended = false;
    last_buffered_result = BufferedResult(-1);
#endif
}

void
BufferedSocket::Reinit(const struct timeval *_read_timeout,
                       const struct timeval *_write_timeout,
                       const BufferedSocketHandler &_handler, void *_ctx)
{
    assert(IsValid());
    assert(IsConnected());
    assert(!expect_more);
    assert(_handler.data != nullptr);
    /* handler method closed() is optional */
    assert(_handler.write != nullptr);
    assert(_handler.error != nullptr);

    read_timeout = _read_timeout;
    write_timeout = _write_timeout;

    handler = &_handler;
    handler_ctx = _ctx;

    direct = false;
}

void
BufferedSocket::Init(struct pool &_pool,
                     BufferedSocket &&src,
                     const struct timeval *_read_timeout,
                     const struct timeval *_write_timeout,
                     const BufferedSocketHandler &_handler, void *_ctx)
{
    assert(_handler.data != nullptr);
    /* handler method closed() is optional */
    assert(_handler.write != nullptr);
    assert(_handler.error != nullptr);

    base.Init(_pool, std::move(src.base),
              buffered_socket_handler, this);

    read_timeout = _read_timeout;
    write_timeout = _write_timeout;

    handler = &_handler;
    handler_ctx = _ctx;

    /* steal the input buffer (after we already stole the socket) */
    input = std::move(src.input);

    direct = false;
    expect_more = false;
    destroyed = false;

#ifndef NDEBUG
    reading = false;
    ended = false;
    last_buffered_result = BufferedResult(-1);
#endif
}

void
BufferedSocket::Destroy()
{
    assert(!base.IsValid());
    assert(!destroyed);

    input.FreeIfDefined(fb_pool_get());

    destroyed = true;
}

bool
BufferedSocket::IsEmpty() const
{
    assert(!ended);

    return input.IsEmpty();
}

bool
BufferedSocket::IsFull() const
{
    assert(!ended);

    return input.IsDefinedAndFull();
}

bool
BufferedSocket::Read(bool _expect_more)
{
    assert(!reading);
    assert(!destroyed);
    assert(!ended);

    if (_expect_more) {
        if (!IsConnected() && IsEmpty()) {
            ClosedPrematurely();
            return false;
        }

        expect_more = true;
    }

    return TryRead();
}

ssize_t
BufferedSocket::Write(const void *data, size_t length)
{
    ssize_t nbytes = base.Write(data, length);

    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            ScheduleWrite();
            return WRITE_BLOCKING;
        } else if ((errno == EPIPE || errno == ECONNRESET)) {
            enum write_result r = handler->broken != nullptr
                ? handler->broken(handler_ctx)
                : WRITE_ERRNO;

            if (r == WRITE_BROKEN)
                UnscheduleWrite();

            nbytes = ssize_t(r);
        }
    }

    return nbytes;
}

ssize_t
BufferedSocket::WriteFrom(int other_fd, enum istream_direct other_fd_type,
                          size_t length)
{
    ssize_t nbytes = base.WriteFrom(other_fd, other_fd_type, length);
    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            if (!IsReadyForWriting()) {
                ScheduleWrite();
                return WRITE_BLOCKING;
            }

            /* try again, just in case our fd has become ready between
               the first socket_wrapper_write_from() call and
               fd_ready_for_writing() */
            nbytes = base.WriteFrom(other_fd, other_fd_type, length);
        }
    }

    return nbytes;
}

void
BufferedSocket::ScheduleReadTimeout(bool _expect_more,
                                    const struct timeval *timeout)
{
    assert(!ended);
    assert(!destroyed);

    if (_expect_more)
        expect_more = true;

    read_timeout = timeout;

    if (!input.IsEmpty())
        /* deferred call to Read() to deliver data from the buffer */
        defer_event_add(&defer_read);
    else
        /* the input buffer is empty: wait for more data from the
           socket */
        base.ScheduleRead(timeout);
}
