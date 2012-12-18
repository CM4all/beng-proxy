/*
 * Wrapper for a socket file descriptor with input and output
 * buffer management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "buffered_socket.h"
#include "fifo-buffer.h"
#include "gerrno.h"

#include <limits.h>
#include <errno.h>

static bool
buffered_socket_input_empty(const struct buffered_socket *s)
{
    assert(s != NULL);
    assert(!s->ended);

    return s->input == NULL || fifo_buffer_empty(s->input);
}

static bool
buffered_socket_input_full(const struct buffered_socket *s)
{
    assert(s != NULL);
    assert(!s->ended);

    return s->input != NULL && fifo_buffer_full(s->input);
}

void
buffered_socket_consumed(struct buffered_socket *s, size_t nbytes)
{
    assert(s != NULL);
    assert(!s->ended);
    assert(s->input != NULL);

    fifo_buffer_consume(s->input, nbytes);
}

static bool
buffered_socket_submit_from_buffer(struct buffered_socket *s)
{
    if (s->input == NULL)
        return true;

    size_t length;
    const void *data = fifo_buffer_read(s->input, &length);
    if (data == NULL) {
        assert(buffered_socket_connected(s));
        return true;
    }

    pool_ref(s->base.pool);

    bool result = s->handler->data(data, length, s->handler_ctx);
    const bool valid = buffered_socket_valid(s);
    assert(!result || valid);

    pool_unref(s->base.pool);

    if (!valid)
        /* the buffered_socket object has been destroyed by the
           handler */
        return false;

    if (fifo_buffer_empty(s->input) && !buffered_socket_connected(s)) {
        assert(s->handler->end != NULL);

#ifndef NDEBUG
        s->ended = true;
#endif

        s->handler->end(s->handler_ctx);
        result = false;
    }

    if (result && !buffered_socket_connected(s))
        result = false;

    return result;
}

/**
 * @return true if more data should be read from the socket
 */
static bool
buffered_socket_submit_direct(struct buffered_socket *s)
{
    assert(buffered_socket_connected(s));
    assert(buffered_socket_input_empty(s));

    return s->handler->direct(s->base.fd, s->base.fd_type, s->handler_ctx);
}

static bool
buffered_socket_fill_buffer(struct buffered_socket *s)
{
    assert(buffered_socket_connected(s));

    struct fifo_buffer *buffer = s->input;
    if (buffer == NULL)
        buffer = s->input = fifo_buffer_new(s->base.pool, 8192);

    ssize_t nbytes = socket_wrapper_read_to_buffer(&s->base, buffer, INT_MAX);
    if (gcc_likely(nbytes > 0))
        /* success: data was added to the buffer */
        return true;

    if (nbytes == 0) {
        /* socket closed */

        const size_t remaining = fifo_buffer_available(buffer);

        if (!s->handler->closed(remaining, s->handler_ctx))
            return false;

        assert(!buffered_socket_connected(s));
        assert(s->input == buffer);
        assert(remaining == fifo_buffer_available(buffer));
        assert(s->handler->end != NULL);

        if (fifo_buffer_empty(buffer)) {
#ifndef NDEBUG
            s->ended = true;
#endif

            s->handler->end(s->handler_ctx);
            return false;
        }

        return true;
    }

    if (nbytes == -2) {
        /* input buffer is full */
        socket_wrapper_unschedule_read(&s->base);
        return true;
    }

    if (nbytes == -1) {
        if (errno == EAGAIN) {
            socket_wrapper_schedule_read(&s->base);
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
            socket_wrapper_unschedule_read(&s->base);
            return true;
        }

        if (!buffered_socket_submit_direct(s))
            return false;

        socket_wrapper_schedule_read(&s->base);
        return true;
    } else {
        if (!buffered_socket_fill_buffer(s))
            return false;

        if (!buffered_socket_submit_from_buffer(s))
            return false;

        socket_wrapper_schedule_read(&s->base);
        return true;
    }
}

static bool
buffered_socket_try_read(struct buffered_socket *s)
{
    assert(buffered_socket_connected(s));
    assert(!s->destroyed);
    assert(!s->ended);
    assert(!s->reading);

#ifndef NDEBUG
    struct pool_notify notify;
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
    struct buffered_socket *s = ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return s->handler->write(s->handler_ctx);
}

static bool
buffered_socket_wrapper_read(void *ctx)
{
    struct buffered_socket *s = ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    return buffered_socket_try_read(s);
}

static bool
buffered_socket_wrapper_timeout(void *ctx)
{
    struct buffered_socket *s = ctx;
    assert(!s->destroyed);
    assert(!s->ended);

    if (s->handler->timeout != NULL)
        return s->handler->timeout(s->handler_ctx);

    s->handler->error(g_error_new_literal(errno_quark(), ETIMEDOUT, "Timeout"),
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
    assert(handler != NULL);
    assert(handler->data != NULL);
    /* handler method closed() is optional */
    assert(handler->write != NULL);
    assert(handler->error != NULL);

    socket_wrapper_init(&s->base, pool, fd, fd_type,
                        read_timeout, write_timeout,
                        &buffered_socket_handler, s);

    s->handler = handler;
    s->handler_ctx = ctx;
    s->input = NULL;
    s->direct = false;

#ifndef NDEBUG
    s->reading = false;
    s->ended = false;
    s->destroyed = false;
#endif
}

bool
buffered_socket_empty(const struct buffered_socket *s)
{
    assert(s != NULL);
    assert(!s->ended);

    return s->input == NULL || fifo_buffer_empty(s->input);
}

bool
buffered_socket_full(const struct buffered_socket *s)
{
    return buffered_socket_input_full(s);
}

bool
buffered_socket_read(struct buffered_socket *s)
{
    assert(!s->reading);
    assert(!s->destroyed);
    assert(!s->ended);

    return buffered_socket_try_read(s);
}
