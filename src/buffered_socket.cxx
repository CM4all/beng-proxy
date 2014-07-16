/*
 * Wrapper for a socket file descriptor with input and output
 * buffer management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "buffered_socket.hxx"
#include "fifo-buffer.h"
#include "fb_pool.h"
#include "gerrno.h"

#include <limits.h>
#include <errno.h>

static void
buffered_socket_closed_prematurely(BufferedSocket *s)
{
    GError *error =
        g_error_new_literal(buffered_socket_quark(), 0,
                            "Peer closed the socket prematurely");
    s->handler->error(error, s->handler_ctx);
}

static void
buffered_socket_ended(BufferedSocket *s)
{
    assert(!s->IsConnected());
    assert(!s->ended);

#ifndef NDEBUG
    s->ended = true;
#endif

    if (s->handler->end == nullptr)
        buffered_socket_closed_prematurely(s);
    else
        s->handler->end(s->handler_ctx);
}

static bool
buffered_socket_input_empty(const BufferedSocket *s)
{
    assert(s != nullptr);
    assert(!s->ended);

    return s->input == nullptr || fifo_buffer_empty(s->input);
}

static bool
buffered_socket_input_full(const BufferedSocket *s)
{
    assert(s != nullptr);
    assert(!s->ended);

    return s->input != nullptr && fifo_buffer_full(s->input);
}

int
BufferedSocket::AsFD()
{
    if (!buffered_socket_input_empty(this))
        /* can switch to the raw socket descriptor only if the input
           buffer is empty */
        return -1;

    return base.AsFD();
}

size_t
BufferedSocket::GetAvailable() const
{
    assert(!ended);

    return input != nullptr
        ? fifo_buffer_available(input)
        : 0;
}

void
BufferedSocket::Consumed( size_t nbytes)
{
    assert(!ended);
    assert(input != nullptr);

    fifo_buffer_consume(input, nbytes);
}

/**
 * Invokes the data handler, and takes care for
 * #BufferedResult::AGAIN_OPTIONAL and #BufferedResult::AGAIN_EXPECT.
 */
static BufferedResult
buffered_socket_invoke_data(BufferedSocket *s)
{
    assert(!buffered_socket_input_empty(s));

    bool local_expect_more = false;

    while (true) {
        size_t length;
        const void *data = fifo_buffer_read(s->input, &length);
        data = fifo_buffer_read(s->input, &length);
        if (data == nullptr)
            return s->expect_more || local_expect_more
                ? BufferedResult::MORE
                : BufferedResult::OK;

#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify(&s->base.GetPool(), &notify);
#endif

        BufferedResult result =
            s->handler->data(data, length, s->handler_ctx);

#ifndef NDEBUG
        if (pool_denotify(&notify)) {
            assert(result == BufferedResult::CLOSED);
        } else {
            s->last_buffered_result = result;
            assert((result == BufferedResult::CLOSED) == !s->IsValid());
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

static bool
buffered_socket_submit_from_buffer(BufferedSocket *s)
{
    if (buffered_socket_input_empty(s))
        return true;

    const bool old_expect_more = s->expect_more;
    s->expect_more = false;

    BufferedResult result = buffered_socket_invoke_data(s);
    assert((result == BufferedResult::CLOSED) || s->IsValid());

    switch (result) {
    case BufferedResult::OK:
        assert(fifo_buffer_empty(s->input));
        assert(!s->expect_more);

        if (!s->IsConnected()) {
            buffered_socket_ended(s);
            return false;
        }

        return true;

    case BufferedResult::PARTIAL:
        assert(!fifo_buffer_empty(s->input));

        if (!s->IsConnected())
            return false;

        return true;

    case BufferedResult::MORE:
        s->expect_more = true;

        if (!s->IsConnected()) {
            buffered_socket_closed_prematurely(s);
            return false;
        }

        if (s->IsFull()) {
            GError *error =
                g_error_new_literal(buffered_socket_quark(), 0,
                                    "Input buffer overflow");
            s->handler->error(error, s->handler_ctx);
            return false;
        }

        return true;

    case BufferedResult::AGAIN_OPTIONAL:
    case BufferedResult::AGAIN_EXPECT:
        /* unreachable, has been handled by
           buffered_socket_invoke_data() */
        assert(false);
        gcc_unreachable();

    case BufferedResult::BLOCKING:
        s->expect_more = old_expect_more;
        s->base.UnscheduleRead();
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
static bool
buffered_socket_submit_direct(BufferedSocket *s)
{
    assert(s->IsConnected());
    assert(buffered_socket_input_empty(s));

    const bool old_expect_more = s->expect_more;
    s->expect_more = false;

    const DirectResult result =
        s->handler->direct(s->base.GetFD(), s->base.GetType(), s->handler_ctx);
    switch (result) {
    case DirectResult::OK:
        /* some data was transferred: refresh the read timeout */
        s->base.ScheduleRead(s->read_timeout);
        return true;

    case DirectResult::BLOCKING:
        s->expect_more = old_expect_more;
        s->base.UnscheduleRead();
        return false;

    case DirectResult::EMPTY:
        /* schedule read, but don't refresh timeout of old scheduled
           read */
        if (!s->base.IsReadPending())
            s->base.ScheduleRead(s->read_timeout);
        return true;

    case DirectResult::END:
        buffered_socket_ended(s);
        return false;

    case DirectResult::CLOSED:
        return false;

    case DirectResult::ERRNO:
        s->handler->error(new_error_errno(), s->handler_ctx);
        return false;
    }

    assert(false);
    gcc_unreachable();
}

static bool
buffered_socket_fill_buffer(BufferedSocket *s)
{
    assert(s->IsConnected());

    struct fifo_buffer *buffer = s->input;
    if (buffer == nullptr)
        buffer = s->input = fb_pool_alloc();

    ssize_t nbytes = s->base.ReadToBuffer(*buffer, INT_MAX);
    if (gcc_likely(nbytes > 0)) {
        /* success: data was added to the buffer */
        s->expect_more = false;
        s->got_data = true;

        return true;
    }

    if (nbytes == 0) {
        /* socket closed */

        if (s->expect_more) {
            buffered_socket_closed_prematurely(s);
            return false;
        }

        assert(s->handler->closed != nullptr);

        const size_t remaining = fifo_buffer_available(buffer);

        if (!s->handler->closed(s->handler_ctx) ||
            (s->handler->remaining != nullptr &&
             !s->handler->remaining(remaining, s->handler_ctx)))
            return false;

        assert(!s->IsConnected());
        assert(s->input == buffer);
        assert(remaining == fifo_buffer_available(buffer));

        if (fifo_buffer_empty(buffer)) {
            buffered_socket_ended(s);
            return false;
        }

        return true;
    }

    if (nbytes == -2) {
        /* input buffer is full */
        s->base.UnscheduleRead();
        return true;
    }

    if (nbytes == -1) {
        if (errno == EAGAIN) {
            /* schedule read, but don't refresh timeout of old
               scheduled read */
            if (!s->base.IsReadPending())
                s->base.ScheduleRead(s->read_timeout);
            return true;
        } else {
            GError *error = new_error_errno_msg("recv() failed");
            s->handler->error(error, s->handler_ctx);
            return false;
        }
    }

    return true;
}

static bool
buffered_socket_try_read2(BufferedSocket *s)
{
    assert(s->IsValid());
    assert(!s->destroyed);
    assert(!s->ended);
    assert(s->reading);

    if (!s->IsConnected()) {
        assert(!buffered_socket_input_empty(s));

        buffered_socket_submit_from_buffer(s);
        return false;
    } else if (s->direct) {
        /* empty the remaining buffer before doing direct transfer */
        if (!buffered_socket_submit_from_buffer(s))
            return false;

        if (!s->direct)
            /* meanwhile, the "direct" flag was reverted by the
               handler - try again */
            return buffered_socket_try_read2(s);

        if (!buffered_socket_input_empty(s)) {
            /* there's still data in the buffer, but our handler isn't
               ready for consuming it - stop reading from the
               socket */
            s->base.UnscheduleRead();
            return true;
        }

        return buffered_socket_submit_direct(s);
    } else {
        s->got_data = false;

        if (!buffered_socket_fill_buffer(s))
            return false;

        if (!buffered_socket_submit_from_buffer(s))
            return false;

        if (s->got_data)
            /* refresh the timeout each time data was received */
            s->base.ScheduleRead(s->read_timeout);
        return true;
    }
}

static bool
buffered_socket_try_read(BufferedSocket *s)
{
    assert(s->IsValid());
    assert(!s->destroyed);
    assert(!s->ended);
    assert(!s->reading);

#ifndef NDEBUG
    struct pool_notify_state notify;
    pool_notify(&s->base.GetPool(), &notify);
    s->reading = true;
#endif

    const bool result = buffered_socket_try_read2(s);

#ifndef NDEBUG
    if (!pool_denotify(&notify)) {
        assert(s->reading);
        s->reading = false;
    }
#endif

    return result;
}

/*
 * socket_wrapper handler
 *
 */

static bool
buffered_socket_wrapper_write(void *ctx)
{
    BufferedSocket *s = (BufferedSocket *)ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return s->handler->write(s->handler_ctx);
}

static bool
buffered_socket_wrapper_read(void *ctx)
{
    BufferedSocket *s = (BufferedSocket *)ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return buffered_socket_try_read(s);
}

static bool
buffered_socket_wrapper_timeout(void *ctx)
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

static const struct socket_handler buffered_socket_handler = {
    .read = buffered_socket_wrapper_read,
    .write = buffered_socket_wrapper_write,
    .timeout = buffered_socket_wrapper_timeout,
};

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

    handler = &_handler;
    handler_ctx = _ctx;
    input = nullptr;
    direct = false;
    expect_more = false;

#ifndef NDEBUG
    reading = false;
    ended = false;
    destroyed = false;
    last_buffered_result = BufferedResult(-1);
#endif
}

void
BufferedSocket::Destroy()
{
    assert(!base.IsValid());
    assert(!destroyed);

    if (input != nullptr) {
        fb_pool_free(input);
        input = nullptr;
    }

#ifndef NDEBUG
    destroyed = true;
#endif
}

bool
BufferedSocket::IsEmpty() const
{
    assert(!ended);

    return input == nullptr || fifo_buffer_empty(input);
}

bool
BufferedSocket::IsFull() const
{
    return buffered_socket_input_full(this);
}

bool
BufferedSocket::Read(bool _expect_more)
{
    assert(!reading);
    assert(!destroyed);
    assert(!ended);

    if (_expect_more) {
        if (!IsConnected() &&
            buffered_socket_input_empty(this)) {
            buffered_socket_closed_prematurely(this);
            return false;
        }

        expect_more = true;
    }

    return buffered_socket_try_read(this);
}

ssize_t
BufferedSocket::Write(const void *data, size_t length)
{
    ssize_t nbytes = base.Write(data, length);

    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            ScheduleWrite();
            return WRITE_BLOCKING;
        } else if ((errno == EPIPE || errno == ECONNRESET) &&
                   handler->broken != nullptr &&
                   handler->broken(handler_ctx)) {
            UnscheduleWrite();
            return WRITE_BROKEN;
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
