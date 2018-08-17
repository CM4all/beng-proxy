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

#include "Input.hxx"
#include "Error.hxx"
#include "event/SocketEvent.hxx"
#include "direct.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Result.hxx"
#include "pool/pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "io/Buffered.hxx"
#include "system/Error.hxx"
#include "util/ScopeExit.hxx"
#include "util/Exception.hxx"

#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

static constexpr struct timeval was_input_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

class WasInput final : public Istream {
public:
    int fd;
    SocketEvent event;

    WasInputHandler &handler;

    SliceFifoBuffer buffer;

    uint64_t received = 0, length;

    bool enabled = false;

    bool closed = false, timeout = false, known_length = false;

    WasInput(struct pool &p, EventLoop &event_loop, int _fd,
             WasInputHandler &_handler)
        :Istream(p), fd(_fd),
         event(event_loop, fd, SocketEvent::READ,
               BIND_THIS_METHOD(EventCallback)),
         handler(_handler) {
    }

    void Free(std::exception_ptr ep);

    UnusedIstreamPtr Enable() {
        assert(!enabled);
        enabled = true;
        ScheduleRead();
        return UnusedIstreamPtr(this);
    }

    bool SetLength(uint64_t _length);
    void PrematureThrow(uint64_t _length);
    bool Premature(uint64_t _length);

    bool CanRelease() const {
        return known_length && received == length;
    }

    /**
     * @return false if the #WasInput has been destroyed
     */
    bool ReleasePipe() {
        assert(fd >= 0);
        fd = -1;
        event.Delete();

        return handler.WasInputRelease();
    }

    /**
     * @return false if the #WasInput has been destroyed
     */
    bool CheckReleasePipe() {
        return !CanRelease() || ReleasePipe();
    }

    using Istream::HasHandler;
    using Istream::Destroy;
    using Istream::DestroyError;

    void ScheduleRead() {
        assert(fd >= 0);
        assert(!buffer.IsDefined() || !buffer.IsFull());

        event.Add(timeout ? &was_input_timeout : nullptr);
    }

    void AbortError(std::exception_ptr ep) {
        buffer.FreeIfDefined(fb_pool_get());
        event.Delete();

        /* protect against recursive Free() call within the istream
           handler */
        closed = true;

        handler.WasInputError();
        DestroyError(ep);
    }

    void AbortError(const char *msg) {
        AbortError(std::make_exception_ptr(WasProtocolError(msg)));
    }

    void Eof() {
        assert(known_length);
        assert(received == length);
        assert(!buffer.IsDefined());

        event.Delete();

        handler.WasInputEof();
        DestroyEof();
    }

    bool CheckEof() {
        if (CanRelease() && buffer.IsEmpty()) {
            Eof();
            return true;
        } else
            return false;
    }

    /**
     * Consume data from the input buffer.  Returns true if data has been
     * consumed.
     */
    bool SubmitBuffer() {
        auto r = buffer.Read();
        if (!r.empty()) {
            size_t nbytes = InvokeData(r.data, r.size);
            if (nbytes == 0)
                return false;

            buffer.Consume(nbytes);
            buffer.FreeIfEmpty(fb_pool_get());
        }

        if (CheckEof())
            return false;

        return true;
    }

    /*
     * socket i/o
     *
     */

    bool ReadToBuffer();
    bool TryBuffered();
    bool TryDirect();

    void TryRead() {
        if (CheckDirect(FdType::FD_PIPE)) {
            if (SubmitBuffer())
                TryDirect();
        } else {
            TryBuffered();
        }
    }

    void EventCallback(unsigned events);

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) noexcept override {
        if (known_length)
            return length - received + buffer.GetAvailable();
        else if (partial)
            return buffer.GetAvailable();
        else
            return -1;
    }

    void _Read() noexcept override {
        event.Delete();

        if (SubmitBuffer())
            TryRead();
    }

    void _Close() noexcept override {
        buffer.FreeIfDefined(fb_pool_get());
        event.Delete();

        /* protect against recursive Free() call within the istream
           handler */
        closed = true;

        handler.WasInputClose(received);

        Destroy();
    }
};

inline bool
WasInput::ReadToBuffer()
{
    buffer.AllocateIfNull(fb_pool_get());

    size_t max_length = 4096;
    if (known_length) {
        uint64_t rest = length - received;
        if (rest < (uint64_t)max_length)
            max_length = rest;

        if (max_length == 0)
            /* all the data we need is already in the buffer */
            return true;
    }

    ssize_t nbytes = read_to_buffer(fd, buffer, max_length);
    assert(nbytes != -2);

    if (nbytes == 0) {
        AbortError("server closed the data connection");
        return false;
    }

    if (nbytes < 0) {
        const int e = errno;

        if (e == EAGAIN) {
            buffer.FreeIfEmpty(fb_pool_get());
            ScheduleRead();
            return true;
        }

        AbortError(std::make_exception_ptr(MakeErrno(e,
                                                     "read error on WAS data connection")));
        return false;
    }

    received += nbytes;
    return true;
}

inline bool
WasInput::TryBuffered()
{
    if (fd >= 0) {
        if (!ReadToBuffer())
            return false;

        if (!CheckReleasePipe())
            return false;
    }

    if (SubmitBuffer()) {
        assert(!buffer.IsDefinedAndFull());

        if (fd >= 0)
            ScheduleRead();
    }

    return true;
}

inline bool
WasInput::TryDirect()
{
    assert(buffer.IsEmpty());
    assert(!buffer.IsDefined());

    size_t max_length = 0x1000000;
    if (known_length) {
        uint64_t rest = length - received;
        if (rest < (uint64_t)max_length)
            max_length = rest;
    }

    ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, fd, max_length);
    if (nbytes == ISTREAM_RESULT_EOF || nbytes == ISTREAM_RESULT_BLOCKING ||
        nbytes == ISTREAM_RESULT_CLOSED)
        return false;

    if (nbytes < 0) {
        const int e = errno;

        if (e == EAGAIN) {
            ScheduleRead();
            return false;
        }

        AbortError(std::make_exception_ptr(MakeErrno(e,
                                                     "read error on WAS data connection")));
        return false;
    }

    received += nbytes;

    if (!CheckReleasePipe())
        return false;

    if (CheckEof())
        return false;

    ScheduleRead();
    return true;
}

/*
 * libevent callback
 *
 */

inline void
WasInput::EventCallback(unsigned events)
{
    assert(fd >= 0);

    if (gcc_unlikely(events & SocketEvent::TIMEOUT)) {
        AbortError("data receive timeout");
        return;
    }

    TryRead();
}

/*
 * constructor
 *
 */

WasInput *
was_input_new(struct pool &pool, EventLoop &event_loop, int fd,
              WasInputHandler &handler)
{
    assert(fd >= 0);

    return NewFromPool<WasInput>(pool, pool, event_loop, fd,
                                 handler);
}

inline void
WasInput::Free(std::exception_ptr ep)
{
    assert(ep || closed || !enabled);

    buffer.FreeIfDefined(fb_pool_get());

    event.Delete();

    if (!closed && enabled)
        DestroyError(ep);
}

void
was_input_free(WasInput *input, std::exception_ptr ep)
{
    input->Free(ep);
}

void
was_input_free_unused(WasInput *input)
{
    assert(!input->HasHandler());
    assert(!input->closed);
    assert(!input->buffer.IsDefined());

    input->Destroy();
}

UnusedIstreamPtr
was_input_enable(WasInput &input)
{
    return input.Enable();
}

inline bool
WasInput::SetLength(uint64_t _length)
{
    if (known_length) {
        if (_length == length)
            return true;

        // TODO: don't invoke Istream::DestroyError() if not yet enabled
        AbortError("wrong input length announced");
        return false;
    }

    if (_length < received) {
        /* this length must be bogus, because we already received more than that from the socket */
        AbortError("announced length is too small");
        return false;
    }

    length = _length;
    known_length = true;

    if (!CheckReleasePipe())
        return false;

    if (enabled && CheckEof())
        return false;

    return true;
}

bool
was_input_set_length(WasInput *input, uint64_t length)
{
    return input->SetLength(length);
}

void
WasInput::PrematureThrow(uint64_t _length)
{
    buffer.FreeIfDefined(fb_pool_get());
    event.Delete();

    if (known_length && _length > length)
        throw WasProtocolError("announced premature length is too large");

    if (_length < received)
        throw WasProtocolError("announced premature length is too small");

    uint64_t remaining = received - _length;

    while (remaining > 0) {
        uint8_t discard_buffer[4096];
        size_t size = std::min(remaining, uint64_t(sizeof(discard_buffer)));
        ssize_t nbytes = read(fd, discard_buffer, size);
        if (nbytes < 0)
            throw NestException(std::make_exception_ptr(MakeErrno()),
                                WasError("read error on WAS data connection"));

        if (nbytes == 0)
            throw WasProtocolError("server closed the WAS data connection");

        remaining -= nbytes;
    }
}

inline bool
WasInput::Premature(uint64_t _length)
{
    bool result = false;
    try {
        PrematureThrow(_length);
        result = true;
        throw WasProtocolError("premature end of WAS response");
    } catch (...) {
        DestroyError(std::current_exception());
    }

    return result;
}

bool
was_input_premature(WasInput *input, uint64_t length)
{
    return input->Premature(length);
}

void
was_input_premature_throw(WasInput *input, uint64_t length)
{
    AtScopeExit(input) { input->Destroy(); };
    input->PrematureThrow(length);
    throw WasProtocolError("premature end of WAS response");
}
