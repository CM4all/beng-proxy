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

#include "sink_header.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "util/Cancellable.hxx"
#include "util/ByteOrder.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <stdint.h>

class HeaderSink final : public ForwardIstream, Cancellable {
    enum {
        SIZE, HEADER, CALLBACK, DATA
    } state = SIZE;

    unsigned char size_buffer[4];

    unsigned char *buffer;
    size_t size, position = 0;

    /**
     * How much data of the input is pending to be consumed?  Only
     * valid while state==CALLBACK.
     */
    size_t pending;

    const struct sink_header_handler *handler;
    void *handler_ctx;

public:
    HeaderSink(struct pool &_pool, UnusedIstreamPtr _input,
               const struct sink_header_handler &_handler, void *_ctx,
               CancellablePointer &cancel_ptr)
        :ForwardIstream(_pool, std::move(_input)),
         handler(&_handler), handler_ctx(_ctx) {
        cancel_ptr = *this;
    }

    void Read() noexcept {
        input.Read();
    }

private:
    size_t InvokeCallback(size_t consumed);

    size_t ConsumeSize(const void *data, size_t length);
    size_t ConsumeHeader(const void *data, size_t length);

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) noexcept override;

    void _Read() noexcept override {
        if (state == HeaderSink::CALLBACK)
            /* workaround: when invoking the callback from the data()
               handler, it would be illegal to call header->input again */
            return;

        ForwardIstream::_Read();
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        input.Close();
        Destroy();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

size_t
HeaderSink::InvokeCallback(size_t consumed)
{
    assert(state == SIZE || state == HEADER);

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    /* the base value has been set by sink_header_input_data() */
    pending += consumed;

    state = CALLBACK;
    handler->done(buffer, size,
                  UnusedIstreamPtr(this),
                  handler_ctx);

    if (input.IsDefined()) {
        state = DATA;
        input.SetDirect(GetHandlerDirect());
    } else
        /* we have been closed meanwhile; bail out */
        consumed = 0;

    return consumed;
}

inline size_t
HeaderSink::ConsumeSize(const void *data, size_t length)
{
    assert(position < sizeof(size_buffer));

    if (length > sizeof(size_buffer) - position)
        length = sizeof(size_buffer) - position;

    memcpy(size_buffer + position, data, length);
    position += length;

    if (position < sizeof(size_buffer))
        return length;

    const uint32_t *size_p = (const uint32_t *)(const void *)size_buffer;
    size = FromBE32(*size_p);
    if (size > 0x100000) {
        /* header too large */
        input.Close();

        handler->error(std::make_exception_ptr(std::runtime_error("header is too large")),
                       handler_ctx);
        Destroy();
        return 0;
    }

    if (size > 0) {
        buffer = (unsigned char *)p_malloc(&GetPool(), size);
        state = HeaderSink::HEADER;
        position = 0;
    } else {
        /* header empty: don't allocate, invoke callback now */

        buffer = nullptr;

        length = InvokeCallback(length);
    }

    return length;
}

inline size_t
HeaderSink::ConsumeHeader(const void *data, size_t length)
{
    size_t nbytes = size - position;

    assert(position < size);

    if (nbytes > length)
        nbytes = length;

    memcpy(buffer + position, data, nbytes);
    position += nbytes;

    if (position < size)
        return nbytes;

    return InvokeCallback(nbytes);
}


/*
 * istream handler
 *
 */

inline size_t
HeaderSink::OnData(const void *data0, size_t length)
{
    const unsigned char *data = (const unsigned char *)data0;
    size_t consumed = 0, nbytes;

    if (state == DATA)
        return InvokeData(data, length);

    if (state == SIZE) {
        pending = 0; /* just in case the callback is invoked */

        consumed = ConsumeSize(data, length);
        if (consumed == 0)
            return 0;

        if (consumed == length)
            return length;

        data += consumed;
        length -= consumed;
    }

    if (state == HEADER) {
        pending = consumed; /* just in case the callback is invoked */

        nbytes = ConsumeHeader(data, length);
        if (nbytes == 0)
            return 0;

        consumed += nbytes;
        if (consumed == length)
            return length;

        data += nbytes;
        length -= nbytes;
    }

    assert(consumed > 0);

    if (state == DATA && length > 0) {
        const ScopePoolRef ref(GetPool() TRACE_ARGS);

        nbytes = InvokeData(data, length);
        if (nbytes == 0 && !input.IsDefined())
            consumed = 0;
        else
            consumed += nbytes;
    }

    return consumed;
}

inline ssize_t
HeaderSink::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(state == DATA);

    return ForwardIstream::OnDirect(type, fd, max_length);
}

void
HeaderSink::OnEof() noexcept
{
    switch (state) {
    case SIZE:
    case HEADER:
        handler->error(std::make_exception_ptr(std::runtime_error("premature end of file")),
                       handler_ctx);
        Destroy();
        break;

    case CALLBACK:
        assert(false);
        gcc_unreachable();

    case DATA:
        DestroyEof();
        break;
    }
}

void
HeaderSink::OnError(std::exception_ptr ep) noexcept
{
    switch (state) {
    case SIZE:
    case HEADER:
        handler->error(ep, handler_ctx);
        Destroy();
        break;

    case CALLBACK:
        assert(false);
        gcc_unreachable();

    case DATA:
        DestroyError(ep);
        break;
    }
}

/*
 * istream implementation
 *
 */

off_t
HeaderSink::_GetAvailable(bool partial) noexcept
{
    off_t available = ForwardIstream::_GetAvailable(partial);

    if (available >= 0 && state == HeaderSink::CALLBACK) {
        if (available < (off_t)pending) {
            assert(partial);

            return -1;
        }

        available -= pending;
    }

    return available;
}

/*
 * constructor
 *
 */

HeaderSink &
sink_header_new(struct pool &pool, UnusedIstreamPtr input,
                const struct sink_header_handler &handler, void *ctx,
                CancellablePointer &cancel_ptr) noexcept
{
    assert(handler.done != nullptr);
    assert(handler.error != nullptr);

    return *NewIstream<HeaderSink>(pool, std::move(input),
                                   handler, ctx, cancel_ptr);
}

void
sink_header_read(HeaderSink &sink) noexcept
{
    sink.Read();
}
