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

static BufferedResult
filtered_socket_bs_data(const void *buffer, size_t size, void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

    return s->filter->data(buffer, size, s->filter_ctx);
}

static bool
filtered_socket_bs_closed(void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

    return s->filter->closed(s->filter_ctx);
}

static bool
filtered_socket_bs_remaining(size_t remaining, void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

    return s->filter->remaining(remaining, s->filter_ctx);
}

static bool
filtered_socket_bs_write(void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

    return s->filter->internal_write(s->filter_ctx);
}

static void
filtered_socket_bs_end(void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

    s->filter->end(s->filter_ctx);
}

static bool
filtered_socket_bs_timeout(void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

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
    FilteredSocket *s = (FilteredSocket *)ctx;

    return s->handler->broken != nullptr && s->handler->broken(s->handler_ctx);
}

static void
filtered_socket_bs_error(GError *error, void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

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
FilteredSocket::Init(struct pool &pool,
                     int fd, enum istream_direct fd_type,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const SocketFilter *_filter, void *_filter_ctx,
                     const BufferedSocketHandler &__handler,
                     void *_handler_ctx)
{
    const BufferedSocketHandler *_handler = &__handler;

    filter = _filter;
    filter_ctx = _filter_ctx;

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

        handler = _handler;
        handler_ctx = _handler_ctx;

        _handler = &filtered_socket_bs_handler;
        _handler_ctx = this;
    }

    base.Init(&pool, fd, fd_type,
              read_timeout, write_timeout,
              _handler, _handler_ctx);

#ifndef NDEBUG
    ended = false;
#endif

    drained = true;

    if (filter != nullptr)
        filter->init(*this, filter_ctx);
}

void
FilteredSocket::Destroy()
{
    if (filter != nullptr) {
        filter->close(filter_ctx);
        filter = nullptr;
    }

    base.Destroy();
}

bool
FilteredSocket::IsEmpty() const
{
    return filter != nullptr
        ? filter->is_empty(filter_ctx)
        : base.IsEmpty();
}

bool
FilteredSocket::IsFull() const
{
    return filter != nullptr
        ? filter->is_full(filter_ctx)
        : base.IsFull();
}

size_t
FilteredSocket::GetAvailable() const
{
    return filter != nullptr
        ? filter->available(filter_ctx)
        : base.GetAvailable();
}

void
FilteredSocket::Consumed(size_t nbytes)
{
    if (filter != nullptr)
        filter->consumed(nbytes, filter_ctx);
    else
        base.Consumed(nbytes);
}

bool
FilteredSocket::Read(bool expect_more)
{
    if (filter != nullptr)
        return filter->read(expect_more, filter_ctx);
    else
        return base.Read(expect_more);
}

ssize_t
FilteredSocket::Write(const void *data, size_t length)
{
    return filter != nullptr
        ? filter->write(data, length, filter_ctx)
        : base.Write(data, length);
}

bool
FilteredSocket::InternalDrained()
{
    assert(filter != nullptr);
    assert(IsConnected());

    if (drained || handler->drained == nullptr)
        return true;

    drained = true;
    return handler->drained(handler_ctx);
}
