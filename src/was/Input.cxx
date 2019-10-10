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

#include "Input.hxx"
#include "Error.hxx"
#include "event/SocketEvent.hxx"
#include "direct.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Result.hxx"
#include "istream/Bucket.hxx"
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

class WasInput final : Istream {
    FileDescriptor fd;
    SocketEvent event;

    WasInputHandler &handler;

    SliceFifoBuffer buffer;

    uint64_t received = 0, length;

    bool enabled = false;

    bool closed = false, known_length = false;

public:
    WasInput(struct pool &p, EventLoop &event_loop, FileDescriptor _fd,
             WasInputHandler &_handler) noexcept
        :Istream(p), fd(_fd),
         event(event_loop, BIND_THIS_METHOD(EventCallback),
               SocketDescriptor::FromFileDescriptor(fd)),
         handler(_handler) {
    }

    void Free(std::exception_ptr ep) noexcept;

    using Istream::Destroy;

    void DestroyUnused() noexcept {
        assert(!HasHandler());
        assert(!closed);
        assert(!buffer.IsDefined());

        Destroy();
    }

    UnusedIstreamPtr Enable() noexcept {
        assert(!enabled);
        enabled = true;
        ScheduleRead();
        return UnusedIstreamPtr(this);
    }

    void Disable() noexcept {
        event.Cancel();
    }

    bool SetLength(uint64_t _length) noexcept;
    void PrematureThrow(uint64_t _length);
    bool Premature(uint64_t _length) noexcept;

private:
    bool CanRelease() const {
        return known_length && received == length;
    }

    /**
     * @return false if the #WasInput has been destroyed
     */
    bool ReleasePipe() noexcept {
        assert(fd.IsDefined());
        fd.SetUndefined();
        event.Cancel();

        return handler.WasInputRelease();
    }

    /**
     * @return false if the #WasInput has been destroyed
     */
    bool CheckReleasePipe() noexcept {
        return !CanRelease() || ReleasePipe();
    }

    void ScheduleRead() noexcept {
        assert(fd.IsDefined());
        assert(!buffer.IsDefined() || !buffer.IsFull());

        event.ScheduleRead();
    }

    void AbortError(std::exception_ptr ep) noexcept {
        buffer.FreeIfDefined();
        event.Cancel();

        /* protect against recursive Free() call within the istream
           handler */
        closed = true;

        handler.WasInputError();
        DestroyError(ep);
    }

    void AbortError(const char *msg) noexcept {
        AbortError(std::make_exception_ptr(WasProtocolError(msg)));
    }

    void Eof() noexcept {
        assert(known_length);
        assert(received == length);
        assert(!buffer.IsDefined());

        event.Cancel();

        handler.WasInputEof();
        DestroyEof();
    }

    bool CheckEof() noexcept {
        if (CanRelease() && buffer.empty()) {
            Eof();
            return true;
        } else
            return false;
    }

    /**
     * Consume data from the input buffer.
     *
     * @return false if the handler blocks or if this object has been
     * destroyed
     */
    bool SubmitBuffer() noexcept {
        auto r = buffer.Read();
        if (!r.empty()) {
            size_t nbytes = InvokeData(r.data, r.size);
            if (nbytes == 0)
                return false;

            buffer.Consume(nbytes);
            buffer.FreeIfEmpty();
        }

        if (CheckEof())
            return false;

        return true;
    }

    /*
     * socket i/o
     *
     */

    /**
     * Throws on error.
     */
    void ReadToBuffer();

    bool TryBuffered() noexcept;
    bool TryDirect() noexcept;

    void TryRead() noexcept {
        if (CheckDirect(FdType::FD_PIPE)) {
            if (SubmitBuffer() && buffer.empty())
                TryDirect();
        } else {
            TryBuffered();
        }
    }

    void EventCallback(unsigned events) noexcept;

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
        event.Cancel();

        if (SubmitBuffer())
            TryRead();
    }

    void _FillBucketList(IstreamBucketList &list) override;
    size_t _ConsumeBucketList(size_t nbytes) noexcept override;

    void _Close() noexcept override {
        buffer.FreeIfDefined();
        event.Cancel();

        /* protect against recursive Free() call within the istream
           handler */
        closed = true;

        handler.WasInputClose(received);

        Destroy();
    }
};

void
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
            return;
    }

    ssize_t nbytes = read_to_buffer(fd.Get(), buffer, max_length);
    assert(nbytes != -2);

    if (nbytes == 0)
        throw WasProtocolError("server closed the data connection");

    if (nbytes < 0) {
        const int e = errno;

        if (e == EAGAIN) {
            buffer.FreeIfEmpty();
            ScheduleRead();
            return;
        }

        throw MakeErrno(e, "read error on WAS data connection");
    }

    received += nbytes;

    if (buffer.IsFull())
        event.CancelRead();
}

inline bool
WasInput::TryBuffered() noexcept
{
    if (fd.IsDefined()) {
        try {
            ReadToBuffer();
        } catch (...) {
            AbortError(std::current_exception());
            return false;
        }

        if (!CheckReleasePipe())
            return false;
    }

    if (SubmitBuffer()) {
        assert(!buffer.IsDefinedAndFull());

        if (fd.IsDefined())
            ScheduleRead();
    }

    return true;
}

inline bool
WasInput::TryDirect() noexcept
{
    assert(buffer.empty());
    assert(!buffer.IsDefined());

    size_t max_length = 0x1000000;
    if (known_length) {
        uint64_t rest = length - received;
        if (rest < (uint64_t)max_length)
            max_length = rest;
    }

    ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, fd.Get(), max_length);
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
WasInput::EventCallback(unsigned) noexcept
{
    assert(fd.IsDefined());

    TryRead();
}

/*
 * constructor
 *
 */

WasInput *
was_input_new(struct pool &pool, EventLoop &event_loop, FileDescriptor fd,
              WasInputHandler &handler) noexcept
{
    assert(fd.IsDefined());

    return NewFromPool<WasInput>(pool, pool, event_loop, fd,
                                 handler);
}

inline void
WasInput::Free(std::exception_ptr ep) noexcept
{
    assert(ep || closed || !enabled);

    buffer.FreeIfDefined();

    event.Cancel();

    if (!closed && enabled)
        DestroyError(ep);
}

void
was_input_free(WasInput *input, std::exception_ptr ep) noexcept
{
    input->Free(ep);
}

void
was_input_free_unused(WasInput *input) noexcept
{
    input->DestroyUnused();
}

UnusedIstreamPtr
was_input_enable(WasInput &input) noexcept
{
    return input.Enable();
}

void
was_input_disable(WasInput &input) noexcept
{
    input.Disable();
}

inline bool
WasInput::SetLength(uint64_t _length) noexcept
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
was_input_set_length(WasInput *input, uint64_t length) noexcept
{
    return input->SetLength(length);
}

void
WasInput::PrematureThrow(uint64_t _length)
{
    buffer.FreeIfDefined();
    event.Cancel();

    if (known_length && _length > length)
        throw WasProtocolError("announced premature length is too large");

    if (_length < received)
        throw WasProtocolError("announced premature length is too small");

    uint64_t remaining = received - _length;

    while (remaining > 0) {
        uint8_t discard_buffer[4096];
        size_t size = std::min(remaining, uint64_t(sizeof(discard_buffer)));
        ssize_t nbytes = fd.Read(discard_buffer, size);
        if (nbytes < 0)
            throw NestException(std::make_exception_ptr(MakeErrno("Read error")),
                                WasError("read error on WAS data connection"));

        if (nbytes == 0)
            throw WasProtocolError("server closed the WAS data connection");

        remaining -= nbytes;
    }
}

inline bool
WasInput::Premature(uint64_t _length) noexcept
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
was_input_premature(WasInput *input, uint64_t length) noexcept
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

void
WasInput::_FillBucketList(IstreamBucketList &list)
{
    auto r = buffer.Read();
    if (r.empty()) {
        if (!fd.IsDefined())
            return;

        ReadToBuffer();

        if (!CheckReleasePipe())
            // TODO: deal with this condition properly or improve error message
            throw std::runtime_error("WAS peer failed");

        r = buffer.Read();
        if (r.empty()) {
            list.SetMore();
            return;
        }
    }

    list.Push(r.ToVoid());

    if (!known_length || received < length)
        list.SetMore();
}

size_t
WasInput::_ConsumeBucketList(size_t nbytes) noexcept
{
    size_t consumed = std::min(buffer.GetAvailable(), nbytes);

    buffer.Consume(consumed);
    buffer.FreeIfEmpty();

    Consumed(consumed);
    return consumed;
}
