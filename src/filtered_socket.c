/*
 * Wrapper for a socket descriptor with (optional) filter for input
 * and output.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "filtered_socket.h"
#include "fifo-buffer.h"
#include "fb_pool.h"

#include <string.h>
#include <errno.h>

/*
 * buffered_socket_handler
 *
 */

static enum buffered_result
filtered_socket_bs_data(const void *buffer, size_t size, void *ctx)
{
    struct filtered_socket *s = ctx;

    return s->filter->data(buffer, size, s->filter_ctx);
}

static bool
filtered_socket_bs_closed(void *ctx)
{
    struct filtered_socket *s = ctx;

    return s->filter->closed(s->filter_ctx);
}

static bool
filtered_socket_bs_remaining(size_t remaining, void *ctx)
{
    struct filtered_socket *s = ctx;

    return s->filter->remaining(remaining, s->filter_ctx);
}

static bool
filtered_socket_bs_write(void *ctx)
{
    struct filtered_socket *s = ctx;

    return s->filter->internal_write(s->filter_ctx);
}

static void
filtered_socket_bs_end(void *ctx)
{
    struct filtered_socket *s = ctx;

    s->filter->end(s->filter_ctx);
}

static bool
filtered_socket_bs_timeout(void *ctx)
{
    struct filtered_socket *s = ctx;

    // TODO: let handler intercept this call
    if (s->handler->timeout != NULL)
        return s->handler->timeout(s->handler_ctx);
    else {
        s->handler->error(g_error_new_literal(buffered_socket_quark(), 0,
                                              "Timeout"),
                          s->handler_ctx);
        return false;
    }
}

static bool
filtered_socket_bs_broken(void *ctx)
{
    struct filtered_socket *s = ctx;

    return s->handler->broken != NULL && s->handler->broken(s->handler_ctx);
}

static void
filtered_socket_bs_error(GError *error, void *ctx)
{
    struct filtered_socket *s = ctx;

    s->handler->error(error, s->handler_ctx);
}

static const struct buffered_socket_handler filtered_socket_bs_handler = {
    .data = filtered_socket_bs_data,
    .closed = filtered_socket_bs_closed,
    .remaining = filtered_socket_bs_remaining,
    .end = filtered_socket_bs_end,
    .write = filtered_socket_bs_write,
    .timeout = filtered_socket_bs_timeout,
    .broken = filtered_socket_bs_broken,
    .error = filtered_socket_bs_error,
};

/*
 * constructor
 *
 */

void
filtered_socket_init(struct filtered_socket *s, struct pool *pool,
                     int fd, enum istream_direct fd_type,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const struct socket_filter *filter,
                     void *filter_ctx,
                     const struct buffered_socket_handler *handler,
                     void *handler_ctx)
{
    s->filter = filter;
    s->filter_ctx = filter_ctx;

    if (filter != NULL) {
        assert(filter->init != NULL);
        assert(filter->data != NULL);
        assert(filter->is_empty != NULL);
        assert(filter->is_full != NULL);
        assert(filter->available != NULL);
        assert(filter->consumed != NULL);
        assert(filter->read != NULL);
        assert(filter->write != NULL);
        assert(filter->internal_write != NULL);
        assert(filter->closed != NULL);
        assert(filter->close != NULL);

        s->handler = handler;
        s->handler_ctx = handler_ctx;

        handler = &filtered_socket_bs_handler;
        handler_ctx = s;
    }

    buffered_socket_init(&s->base, pool, fd, fd_type,
                         read_timeout, write_timeout,
                         handler, handler_ctx);

    if (filter != NULL)
        filter->init(s, filter_ctx);
}

void
filtered_socket_destroy(struct filtered_socket *s)
{
    if (s->filter != NULL) {
        s->filter->close(s->filter_ctx);
        s->filter = NULL;
    }

    buffered_socket_destroy(&s->base);
}

bool
filtered_socket_empty(const struct filtered_socket *s)
{
    return s->filter != NULL
        ? s->filter->is_empty(s->filter_ctx)
        : buffered_socket_empty(&s->base);
}

bool
filtered_socket_full(const struct filtered_socket *s)
{
    return s->filter != NULL
        ? s->filter->is_full(s->filter_ctx)
        : buffered_socket_full(&s->base);
}

size_t
filtered_socket_available(const struct filtered_socket *s)
{
    return s->filter != NULL
        ? s->filter->available(s->filter_ctx)
        : buffered_socket_available(&s->base);
}

void
filtered_socket_consumed(struct filtered_socket *s, size_t nbytes)
{
    if (s->filter != NULL)
        s->filter->consumed(nbytes, s->filter_ctx);
    else
        buffered_socket_consumed(&s->base, nbytes);
}

bool
filtered_socket_read(struct filtered_socket *s, bool expect_more)
{
    if (s->filter != NULL)
        return s->filter->read(expect_more, s->filter_ctx);
    else
        return buffered_socket_read(&s->base, expect_more);
}

ssize_t
filtered_socket_write(struct filtered_socket *s,
                      const void *data, size_t length)
{
    return s->filter != NULL
        ? s->filter->write(data, length, s->filter_ctx)
        : buffered_socket_write(&s->base, data, length);
}
