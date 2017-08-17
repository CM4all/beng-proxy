/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "filtered_socket.hxx"

#include <utility>
#include <stdexcept>

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

    return s->InvokeClosed();
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
    return s->InvokeTimeout();
}

static enum write_result
filtered_socket_bs_broken(void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

    return s->handler->broken != nullptr
        ? s->handler->broken(s->handler_ctx)
        : WRITE_ERRNO;
}

static void
filtered_socket_bs_error(std::exception_ptr ep, void *ctx)
{
    FilteredSocket *s = (FilteredSocket *)ctx;

    s->handler->error(ep, s->handler_ctx);
}

static constexpr BufferedSocketHandler filtered_socket_bs_handler = {
    .data = filtered_socket_bs_data,
    .direct = nullptr,
    .closed = filtered_socket_bs_closed,
    .remaining = filtered_socket_bs_remaining,
    .end = filtered_socket_bs_end,
    .write = filtered_socket_bs_write,
    .drained = nullptr,
    .timeout = filtered_socket_bs_timeout,
    .broken = filtered_socket_bs_broken,
    .error = filtered_socket_bs_error,
};

/*
 * constructor
 *
 */

void
FilteredSocket::Init(SocketDescriptor fd, FdType fd_type,
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

    base.Init(fd, fd_type,
              read_timeout, write_timeout,
              *_handler, _handler_ctx);

#ifndef NDEBUG
    ended = false;
#endif

    drained = true;

    if (filter != nullptr)
        filter->init(*this, filter_ctx);
}

void
FilteredSocket::Reinit(const struct timeval *read_timeout,
                       const struct timeval *write_timeout,
                       const BufferedSocketHandler &__handler,
                       void *_handler_ctx)
{
    const BufferedSocketHandler *_handler = &__handler;

    if (filter != nullptr) {
        handler = _handler;
        handler_ctx = _handler_ctx;

        _handler = &filtered_socket_bs_handler;
        _handler_ctx = this;
    }

    base.Reinit(read_timeout, write_timeout,
                *_handler, _handler_ctx);
}

void
FilteredSocket::Init(FilteredSocket &&src,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const BufferedSocketHandler &__handler,
                     void *_handler_ctx)
{
    const BufferedSocketHandler *_handler = &__handler;

    /* steal the filter */
    filter = src.filter;
    filter_ctx = src.filter_ctx;
    src.filter = nullptr;

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

    base.Init(std::move(src.base),
              read_timeout, write_timeout,
              *_handler, _handler_ctx);

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

WritableBuffer<void>
FilteredSocket::ReadBuffer() const
{
    return filter != nullptr
        // TODO: read from filter output buffer?
        ? nullptr
        : base.ReadBuffer();
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

bool
FilteredSocket::InvokeTimeout()
{
    if (handler->timeout != nullptr)
        return handler->timeout(handler_ctx);
    else {
        handler->error(std::make_exception_ptr(std::runtime_error("Timeout")),
                       handler_ctx);
        return false;
    }
}
