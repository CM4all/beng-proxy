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

#include "sink_buffer.hxx"
#include "istream.hxx"
#include "UnusedPtr.hxx"
#include "Sink.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

struct BufferSink final : IstreamSink, Cancellable {
    struct pool *pool;

    unsigned char *const buffer;
    const size_t size;
    size_t position = 0;

    const struct sink_buffer_handler *const handler;
    void *handler_ctx;

    BufferSink(struct pool &_pool, UnusedIstreamPtr _input, size_t available,
               const struct sink_buffer_handler &_handler, void *ctx,
               CancellablePointer &cancel_ptr)
        :IstreamSink(std::move(_input), FD_ANY), pool(&_pool),
         buffer((unsigned char *)p_malloc(pool, available)),
         size(available),
         handler(&_handler), handler_ctx(ctx) {
        cancel_ptr = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

/*
 * istream handler
 *
 */

size_t
BufferSink::OnData(const void *data, size_t length) noexcept
{
    assert(position < size);
    assert(length <= size - position);

    memcpy(buffer + position, data, length);
    position += length;

    return length;
}

ssize_t
BufferSink::OnDirect(FdType type, int fd, size_t max_length) noexcept
{
    size_t length = size - position;
    if (length > max_length)
        length = max_length;

    ssize_t nbytes = IsAnySocket(type)
        ? recv(fd, buffer + position, length, MSG_DONTWAIT)
        : read(fd, buffer + position, length);
    if (nbytes > 0)
        position += (size_t)nbytes;

    return nbytes;
}

void
BufferSink::OnEof() noexcept
{
    assert(position == size);

    handler->done(buffer, size, handler_ctx);
}

void
BufferSink::OnError(std::exception_ptr ep) noexcept
{
    handler->error(ep, handler_ctx);
}


/*
 * async operation
 *
 */

void
BufferSink::Cancel() noexcept
{
    input.Close();
}


/*
 * constructor
 *
 */

void
sink_buffer_new(struct pool &pool, UnusedIstreamPtr input,
                const struct sink_buffer_handler &handler, void *ctx,
                CancellablePointer &cancel_ptr)
{
    static char empty_buffer[1];

    assert(handler.done != nullptr);
    assert(handler.error != nullptr);

    off_t available = input.GetAvailable(false);
    if (available == -1 || available >= 0x10000000) {
        input.Clear();

        handler.error(std::make_exception_ptr(std::runtime_error(available < 0
                                                                 ? "unknown stream length"
                                                                 : "stream is too large")),
                      ctx);
        return;
    }

    if (available == 0) {
        input.Clear();
        handler.done(empty_buffer, 0, ctx);
        return;
    }

    NewFromPool<BufferSink>(pool, pool, std::move(input), available,
                            handler, ctx, cancel_ptr);
}
