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

#include "istream_later.hxx"
#include "ForwardIstream.hxx"
#include "event/DeferEvent.hxx"

class LaterIstream final : public ForwardIstream {
    DeferEvent defer_event;

public:
    LaterIstream(struct pool &_pool, Istream &_input, EventLoop &event_loop)
        :ForwardIstream(_pool, _input),
         defer_event(event_loop, BIND_THIS_METHOD(OnDeferred))
    {
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return -1;
    }

    off_t _Skip(gcc_unused off_t length) override {
        return -1;
    }

    void _Read() override {
        Schedule();
    }

    int _AsFd() override {
        return -1;
    }

    void _Close() override {
        defer_event.Cancel();

        /* input can only be nullptr during the eof callback delay */
        if (HasInput())
            input.Close();

        Destroy();
    }

    /* virtual methods from class IstreamHandler */

    void OnEof() override {
        ClearInput();
        Schedule();
    }

    void OnError(std::exception_ptr ep) override {
        defer_event.Cancel();
        ForwardIstream::OnError(ep);
    }

private:
    void Schedule() {
        defer_event.Schedule();
    }

    void OnDeferred() {
        if (!HasInput())
            DestroyEof();
        else
            ForwardIstream::_Read();
    }
};

Istream *
istream_later_new(struct pool &pool, Istream &input, EventLoop &event_loop)
{
    return NewIstream<LaterIstream>(pool, input, event_loop);
}
