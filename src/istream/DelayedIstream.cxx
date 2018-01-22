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

#include "DelayedIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "event/DeferEvent.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>
#include <string.h>

class DelayedIstream final : public ForwardIstream {
    CancellablePointer cancel_ptr;

    DeferEvent defer_read;

public:
    explicit DelayedIstream(struct pool &p, EventLoop &event_loop) noexcept
        :ForwardIstream(p),
         defer_read(event_loop, BIND_THIS_METHOD(DeferredRead)) {
    }

    ~DelayedIstream() noexcept {
        defer_read.Cancel();
    }

    CancellablePointer &GetCancellablePointer() {
        return cancel_ptr;
    }

    void Set(UnusedIstreamPtr _input) {
        assert(!HasInput());

        SetInput(std::move(_input), GetHandlerDirect());

        if (HasHandler())
            defer_read.Schedule();
    }

    void SetEof() {
        assert(!HasInput());

        DestroyEof();
    }

    void SetError(std::exception_ptr ep) {
        assert(!HasInput());

        DestroyError(ep);
    }

private:
    void DeferredRead() noexcept {
        input.Read();
    }

public:
    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        return HasInput()
            ? ForwardIstream::_GetAvailable(partial)
            : -1;
    }

    void _Read() override {
        if (HasInput())
            ForwardIstream::_Read();
    }

    int _AsFd() override {
        return HasInput()
            ? ForwardIstream::_AsFd()
            : -1;
    }

    void _Close() noexcept override {
        if (HasInput())
            ForwardIstream::_Close();
        else {
            if (cancel_ptr)
                cancel_ptr.Cancel();

            Destroy();
        }
    }
};

Istream *
istream_delayed_new(struct pool &pool, EventLoop &event_loop) noexcept
{
    return NewIstream<DelayedIstream>(pool, event_loop);
}

CancellablePointer &
istream_delayed_cancellable_ptr(Istream &i_delayed)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    return delayed.GetCancellablePointer();
}

void
istream_delayed_set(Istream &i_delayed, UnusedIstreamPtr input)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    delayed.Set(std::move(input));
}

void
istream_delayed_set_eof(Istream &i_delayed)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    delayed.SetEof();
}

void
istream_delayed_set_abort(Istream &i_delayed, std::exception_ptr ep)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    delayed.SetError(ep);
}
