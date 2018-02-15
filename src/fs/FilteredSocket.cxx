/*
 * Copyright 2007-2018 Content Management AG
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

#include "FilteredSocket.hxx"

#include <utility>
#include <stdexcept>

#include <string.h>
#include <errno.h>

/*
 * buffered_socket_handler
 *
 */

BufferedResult
FilteredSocket::OnBufferedData(const void *buffer, size_t size)
{
    return filter->data(buffer, size, filter_ctx);
}

bool
FilteredSocket::OnBufferedClosed() noexcept
{
    return InvokeClosed();
}

bool
FilteredSocket::OnBufferedRemaining(size_t remaining) noexcept
{
    return filter->remaining(remaining, filter_ctx);
}

bool
FilteredSocket::OnBufferedWrite()
{
    return filter->internal_write(filter_ctx);
}

bool
FilteredSocket::OnBufferedEnd() noexcept
{
    filter->end(filter_ctx);
    return true;
}

bool
FilteredSocket::OnBufferedTimeout() noexcept
{
    // TODO: let handler intercept this call
    return InvokeTimeout();
}

enum write_result
FilteredSocket::OnBufferedBroken() noexcept
{
    return handler->OnBufferedBroken();
}

void
FilteredSocket::OnBufferedError(std::exception_ptr ep) noexcept
{
    handler->OnBufferedError(ep);
}

/*
 * constructor
 *
 */

void
FilteredSocket::Init(SocketDescriptor fd, FdType fd_type,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const SocketFilter *_filter, void *_filter_ctx,
                     BufferedSocketHandler &__handler) noexcept
{
    BufferedSocketHandler *_handler = &__handler;

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

        _handler = this;
    }

    base.Init(fd, fd_type,
              read_timeout, write_timeout,
              *_handler);

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
                       BufferedSocketHandler &__handler) noexcept
{
    BufferedSocketHandler *_handler = &__handler;

    if (filter != nullptr) {
        handler = _handler;

        _handler = this;
    }

    base.Reinit(read_timeout, write_timeout,
                *_handler);
}

void
FilteredSocket::Init(FilteredSocket &&src,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     BufferedSocketHandler &__handler) noexcept
{
    BufferedSocketHandler *_handler = &__handler;

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
        _handler = this;
    }

    base.Init(std::move(src.base),
              read_timeout, write_timeout,
              *_handler);

#ifndef NDEBUG
    ended = false;
#endif

    drained = true;

    if (filter != nullptr)
        filter->init(*this, filter_ctx);
}

void
FilteredSocket::Destroy() noexcept
{
    if (filter != nullptr) {
        filter->close(filter_ctx);
        filter = nullptr;
    }

    base.Destroy();
}

bool
FilteredSocket::IsEmpty() const noexcept
{
    return filter != nullptr
        ? filter->is_empty(filter_ctx)
        : base.IsEmpty();
}

bool
FilteredSocket::IsFull() const noexcept
{
    return filter != nullptr
        ? filter->is_full(filter_ctx)
        : base.IsFull();
}

size_t
FilteredSocket::GetAvailable() const noexcept
{
    return filter != nullptr
        ? filter->available(filter_ctx)
        : base.GetAvailable();
}

WritableBuffer<void>
FilteredSocket::ReadBuffer() const noexcept
{
    return filter != nullptr
        // TODO: read from filter output buffer?
        ? nullptr
        : base.ReadBuffer();
}

void
FilteredSocket::Consumed(size_t nbytes) noexcept
{
    if (filter != nullptr)
        filter->consumed(nbytes, filter_ctx);
    else
        base.Consumed(nbytes);
}

bool
FilteredSocket::Read(bool expect_more) noexcept
{
    if (filter != nullptr)
        return filter->read(expect_more, filter_ctx);
    else
        return base.Read(expect_more);
}

ssize_t
FilteredSocket::Write(const void *data, size_t length) noexcept
{
    return filter != nullptr
        ? filter->write(data, length, filter_ctx)
        : base.Write(data, length);
}

bool
FilteredSocket::InternalDrained() noexcept
{
    assert(filter != nullptr);
    assert(IsConnected());

    if (drained)
        return true;

    drained = true;
    return handler->OnBufferedDrained();
}

bool
FilteredSocket::InvokeTimeout() noexcept
{
    return handler->OnBufferedTimeout();
}
