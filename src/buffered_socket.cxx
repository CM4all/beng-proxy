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
buffered_socket_closed_prematurely(struct buffered_socket *s)
{
    GError *error =
        g_error_new_literal(buffered_socket_quark(), 0,
                            "Peer closed the socket prematurely");
    s->handler->error(error, s->handler_ctx);
}

static void
buffered_socket_ended(struct buffered_socket *s)
{
    assert(!buffered_socket_connected(s));
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
buffered_socket_input_empty(const struct buffered_socket *s)
{
    assert(s != nullptr);
    assert(!s->ended);

    return s->input == nullptr || fifo_buffer_empty(s->input);
}

static bool
buffered_socket_input_full(const struct buffered_socket *s)
{
    assert(s != nullptr);
    assert(!s->ended);

    return s->input != nullptr && fifo_buffer_full(s->input);
}

int
buffered_socket_as_fd(struct buffered_socket *s)
{
    if (!buffered_socket_input_empty(s))
        /* can switch to the raw socket descriptor only if the input
           buffer is empty */
        return -1;

    return s->base.AsFD();
}

size_t
buffered_socket_available(const struct buffered_socket *s)
{
    assert(s != nullptr);
    assert(!s->ended);

    return s->input != nullptr
        ? fifo_buffer_available(s->input)
        : 0;
}

void
buffered_socket_consumed(struct buffered_socket *s, size_t nbytes)
{
    assert(s != nullptr);
    assert(!s->ended);
    assert(s->input != nullptr);

    fifo_buffer_consume(s->input, nbytes);
}

/**
 * Invokes the data handler, and takes care for
 * #BUFFERED_AGAIN_OPTIONAL and #BUFFERED_AGAIN_EXPECT.
 */
static enum buffered_result
buffered_socket_invoke_data(struct buffered_socket *s)
{
    assert(!buffered_socket_input_empty(s));

    bool local_expect_more = false;

    while (true) {
        size_t length;
        const void *data = fifo_buffer_read(s->input, &length);
        data = fifo_buffer_read(s->input, &length);
        if (data == nullptr)
            return s->expect_more || local_expect_more
                ? BUFFERED_MORE
                : BUFFERED_OK;

#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify(s->base.pool, &notify);
#endif

        enum buffered_result result =
            s->handler->data(data, length, s->handler_ctx);

#ifndef NDEBUG
        if (pool_denotify(&notify)) {
            assert(result == BUFFERED_CLOSED);
        } else {
            s->last_buffered_result = result;
            assert((result == BUFFERED_CLOSED) == !buffered_socket_valid(s));
        }
#endif

        if (result == BUFFERED_AGAIN_EXPECT)
            local_expect_more = true;
        else if (result == BUFFERED_AGAIN_OPTIONAL)
            local_expect_more = false;
        else
            return result;
    }
}

static bool
buffered_socket_submit_from_buffer(struct buffered_socket *s)
{
    if (buffered_socket_input_empty(s))
        return true;

    const bool old_expect_more = s->expect_more;
    s->expect_more = false;

    enum buffered_result result = buffered_socket_invoke_data(s);
    assert((result == BUFFERED_CLOSED) || buffered_socket_valid(s));

    switch (result) {
    case BUFFERED_OK:
        assert(fifo_buffer_empty(s->input));
        assert(!s->expect_more);

        if (!buffered_socket_connected(s)) {
            buffered_socket_ended(s);
            return false;
        }

        return true;

    case BUFFERED_PARTIAL:
        assert(!fifo_buffer_empty(s->input));

        if (!buffered_socket_connected(s))
            return false;

        return true;

    case BUFFERED_MORE:
        s->expect_more = true;

        if (!buffered_socket_connected(s)) {
            buffered_socket_closed_prematurely(s);
            return false;
        }

        if (buffered_socket_full(s)) {
            GError *error =
                g_error_new_literal(buffered_socket_quark(), 0,
                                    "Input buffer overflow");
            s->handler->error(error, s->handler_ctx);
            return false;
        }

        return true;

    case BUFFERED_AGAIN_OPTIONAL:
    case BUFFERED_AGAIN_EXPECT:
        /* unreachable, has been handled by
           buffered_socket_invoke_data() */
        assert(false);
        return false;

    case BUFFERED_BLOCKING:
        s->expect_more = old_expect_more;
        s->base.UnscheduleRead();
        return false;

    case BUFFERED_CLOSED:
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
buffered_socket_submit_direct(struct buffered_socket *s)
{
    assert(buffered_socket_connected(s));
    assert(buffered_socket_input_empty(s));

    const bool old_expect_more = s->expect_more;
    s->expect_more = false;

    const enum direct_result result =
        s->handler->direct(s->base.fd, s->base.fd_type, s->handler_ctx);
    switch (result) {
    case DIRECT_OK:
        /* some data was transferred: refresh the read timeout */
        s->base.ScheduleRead(s->read_timeout);
        return true;

    case DIRECT_BLOCKING:
        s->expect_more = old_expect_more;
        s->base.UnscheduleRead();
        return false;

    case DIRECT_EMPTY:
        /* schedule read, but don't refresh timeout of old scheduled
           read */
        if (!s->base.IsReadPending())
            s->base.ScheduleRead(s->read_timeout);
        return true;

    case DIRECT_END:
        buffered_socket_ended(s);
        return false;

    case DIRECT_CLOSED:
        return false;

    case DIRECT_ERRNO:
        s->handler->error(new_error_errno(), s->handler_ctx);
        return false;
    }

    assert(false);
    gcc_unreachable();
}

static bool
buffered_socket_fill_buffer(struct buffered_socket *s)
{
    assert(buffered_socket_connected(s));

    struct fifo_buffer *buffer = s->input;
    if (buffer == nullptr)
        buffer = s->input = fb_pool_alloc();

    ssize_t nbytes = s->base.ReadToBuffer(buffer, INT_MAX);
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

        assert(!buffered_socket_connected(s));
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
buffered_socket_try_read2(struct buffered_socket *s)
{
    assert(buffered_socket_valid(s));
    assert(!s->destroyed);
    assert(!s->ended);
    assert(s->reading);

    if (!buffered_socket_connected(s)) {
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
buffered_socket_try_read(struct buffered_socket *s)
{
    assert(buffered_socket_valid(s));
    assert(!s->destroyed);
    assert(!s->ended);
    assert(!s->reading);

#ifndef NDEBUG
    struct pool_notify_state notify;
    pool_notify(s->base.pool, &notify);
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
    struct buffered_socket *s = (struct buffered_socket *)ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return s->handler->write(s->handler_ctx);
}

static bool
buffered_socket_wrapper_read(void *ctx)
{
    struct buffered_socket *s = (struct buffered_socket *)ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return buffered_socket_try_read(s);
}

static bool
buffered_socket_wrapper_timeout(void *ctx)
{
    struct buffered_socket *s = (struct buffered_socket *)ctx;
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
buffered_socket_init(struct buffered_socket *s, struct pool *pool,
                     int fd, enum istream_direct fd_type,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const struct buffered_socket_handler *handler, void *ctx)
{
    assert(handler != nullptr);
    assert(handler->data != nullptr);
    /* handler method closed() is optional */
    assert(handler->write != nullptr);
    assert(handler->error != nullptr);

    s->base.Init(pool, fd, fd_type,
                 &buffered_socket_handler, s);

    s->read_timeout = read_timeout;
    s->write_timeout = write_timeout;

    s->handler = handler;
    s->handler_ctx = ctx;
    s->input = nullptr;
    s->direct = false;
    s->expect_more = false;

#ifndef NDEBUG
    s->reading = false;
    s->ended = false;
    s->destroyed = false;
    s->last_buffered_result = (buffered_result)-1;
#endif
}

void
buffered_socket_destroy(struct buffered_socket *s)
{
    assert(!s->base.IsValid());
    assert(!s->destroyed);

    if (s->input != nullptr) {
        fb_pool_free(s->input);
        s->input = nullptr;
    }

#ifndef NDEBUG
    s->destroyed = true;
#endif
}

bool
buffered_socket_empty(const struct buffered_socket *s)
{
    assert(s != nullptr);
    assert(!s->ended);

    return s->input == nullptr || fifo_buffer_empty(s->input);
}

bool
buffered_socket_full(const struct buffered_socket *s)
{
    return buffered_socket_input_full(s);
}

bool
buffered_socket_read(struct buffered_socket *s, bool expect_more)
{
    assert(!s->reading);
    assert(!s->destroyed);
    assert(!s->ended);

    if (expect_more) {
        if (!buffered_socket_connected(s) &&
            buffered_socket_input_empty(s)) {
            buffered_socket_closed_prematurely(s);
            return false;
        }

        s->expect_more = true;
    }

    return buffered_socket_try_read(s);
}

ssize_t
buffered_socket_write(struct buffered_socket *s,
                      const void *data, size_t length)
{
    ssize_t nbytes = s->base.Write(data, length);

    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            buffered_socket_schedule_write(s);
            return WRITE_BLOCKING;
        } else if ((errno == EPIPE || errno == ECONNRESET) &&
                   s->handler->broken != nullptr &&
                   s->handler->broken(s->handler_ctx)) {
            buffered_socket_unschedule_write(s);
            return WRITE_BROKEN;
        }
    }

    return nbytes;
}

ssize_t
buffered_socket_write_from(struct buffered_socket *s,
                           int fd, enum istream_direct fd_type,
                           size_t length)
{
    ssize_t nbytes = s->base.WriteFrom(fd, fd_type, length);
    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            if (!buffered_socket_ready_for_writing(s)) {
                buffered_socket_schedule_write(s);
                return WRITE_BLOCKING;
            }

            /* try again, just in case our fd has become ready between
               the first socket_wrapper_write_from() call and
               fd_ready_for_writing() */
            nbytes = s->base.WriteFrom(fd, fd_type, length);
        }
    }

    return nbytes;
}
