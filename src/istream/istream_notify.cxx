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

#include "istream_notify.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"

#include <assert.h>

class NotifyIstream final : public ForwardIstream {
    const struct istream_notify_handler &handler;
    void *const handler_ctx;

public:
    NotifyIstream(struct pool &p, UnusedIstreamPtr _input,
                  const struct istream_notify_handler &_handler, void *_ctx)
        :ForwardIstream(p, std::move(_input)),
         handler(_handler), handler_ctx(_ctx) {}

    /* virtual methods from class Istream */

    void _FillBucketList(IstreamBucketList &list) override {
        try {
            input.FillBucketList(list);
        } catch (...) {
            handler.abort(handler_ctx);
            Destroy();
            throw;
        }
    }

    void _Close() noexcept override {
        handler.close(handler_ctx);
        ForwardIstream::_Close();
    }

    /* virtual methods from class IstreamHandler */

    void OnEof() noexcept override {
        handler.eof(handler_ctx);
        ForwardIstream::OnEof();
    }

    void OnError(std::exception_ptr ep) noexcept override {
        handler.abort(handler_ctx);
        ForwardIstream::OnError(ep);
    }
};

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_notify_new(struct pool &pool, UnusedIstreamPtr input,
                   const struct istream_notify_handler &handler,
                   void *ctx) noexcept
{
    assert(handler.eof != nullptr);
    assert(handler.abort != nullptr);
    assert(handler.close != nullptr);

    return UnusedIstreamPtr(NewIstream<NotifyIstream>(pool, std::move(input), handler, ctx));
}
