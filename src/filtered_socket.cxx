/*
 * Wrapper for a socket descriptor with (optional) filter for input
 * and output.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "filtered_socket.hxx"
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
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    return s->filter->data(buffer, size, s->filter_ctx);
}

static bool
filtered_socket_bs_closed(void *ctx)
{
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    return s->filter->closed(s->filter_ctx);
}

static bool
filtered_socket_bs_remaining(size_t remaining, void *ctx)
{
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    return s->filter->remaining(remaining, s->filter_ctx);
}

static bool
filtered_socket_bs_write(void *ctx)
{
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    return s->filter->internal_write(s->filter_ctx);
}

static void
filtered_socket_bs_end(void *ctx)
{
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    s->filter->end(s->filter_ctx);
}

static bool
filtered_socket_bs_timeout(void *ctx)
{
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    // TODO: let handler intercept this call
    if (s->handler->timeout != nullptr)
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
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    return s->handler->broken != nullptr && s->handler->broken(s->handler_ctx);
}

static void
filtered_socket_bs_error(GError *error, void *ctx)
{
    struct filtered_socket *s = (struct filtered_socket *)ctx;

    s->handler->error(error, s->handler_ctx);
}

static constexpr BufferedSocketHandler filtered_socket_bs_handler = {
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
                     const BufferedSocketHandler *handler,
                     void *handler_ctx)
{
    s->filter = filter;
    s->filter_ctx = filter_ctx;

    if (filter != nullptr) {
        assert(filter->init != nullptr);
        assert(filter->data != nullptr);
        assert(filter->is_empty != nullptr);
        assert(filter->is_full != nullptr);
        assert(filter->available != nullptr);
        assert(filter->consumed != nullptr);
        assert(filter->read != nullptr);
        assert(filter->write != nullptr);
        assert(filter->internal_write != nullptr);
        assert(filter->closed != nullptr);
        assert(filter->close != nullptr);

        s->handler = handler;
        s->handler_ctx = handler_ctx;

        handler = &filtered_socket_bs_handler;
        handler_ctx = s;
    }

    s->base.Init(pool, fd, fd_type,
                 read_timeout, write_timeout,
                 handler, handler_ctx);

#ifndef NDEBUG
    s->ended = false;
#endif

    s->drained = true;

    if (filter != nullptr)
        filter->init(s, filter_ctx);
}

void
filtered_socket_destroy(struct filtered_socket *s)
{
    if (s->filter != nullptr) {
        s->filter->close(s->filter_ctx);
        s->filter = nullptr;
    }

    s->base.Destroy();
}

bool
filtered_socket_empty(const struct filtered_socket *s)
{
    return s->filter != nullptr
        ? s->filter->is_empty(s->filter_ctx)
        : s->base.IsEmpty();
}

bool
filtered_socket_full(const struct filtered_socket *s)
{
    return s->filter != nullptr
        ? s->filter->is_full(s->filter_ctx)
        : s->base.IsFull();
}

size_t
filtered_socket_available(const struct filtered_socket *s)
{
    return s->filter != nullptr
        ? s->filter->available(s->filter_ctx)
        : s->base.GetAvailable();
}

void
filtered_socket_consumed(struct filtered_socket *s, size_t nbytes)
{
    if (s->filter != nullptr)
        s->filter->consumed(nbytes, s->filter_ctx);
    else
        s->base.Consumed(nbytes);
}

bool
filtered_socket_read(struct filtered_socket *s, bool expect_more)
{
    if (s->filter != nullptr)
        return s->filter->read(expect_more, s->filter_ctx);
    else
        return s->base.Read(expect_more);
}

ssize_t
filtered_socket_write(struct filtered_socket *s,
                      const void *data, size_t length)
{
    return s->filter != nullptr
        ? s->filter->write(data, length, s->filter_ctx)
        : s->base.Write(data, length);
}

bool
filtered_socket_internal_drained(struct filtered_socket *s)
{
    assert(s != nullptr);
    assert(s->filter != nullptr);
    assert(filtered_socket_connected(s));

    if (s->drained || s->handler->drained == nullptr)
        return true;

    s->drained = true;
    return s->handler->drained(s->handler_ctx);
}
