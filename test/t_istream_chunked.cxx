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

#include "istream/istream_chunked.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(*pool, "foo_bar_0123456789abcdefghijklmnopqrstuvwxyz").Steal();
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    return istream_chunked_new(*pool, UnusedIstreamPtr(input)).Steal();
}

#define CUSTOM_TEST

struct Custom final : Istream, IstreamHandler {
    bool eof;
    std::exception_ptr error;

    explicit Custom(struct pool &p):Istream(p) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return 1;
    }

    void _Read() override {}

    /* virtual methods from class IstreamHandler */

    size_t OnData(gcc_unused const void *data,
                  gcc_unused size_t length) override {
        InvokeData(" ", 1);
        return 0;
    }

    void OnEof() noexcept override {
        eof = true;
    }

    void OnError(std::exception_ptr ep) noexcept override {
        error = ep;
    }
};

static void
test_custom(EventLoop &, struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    auto *ctx = NewFromPool<Custom>(*pool, *pool);

    auto *chunked = istream_chunked_new(*pool, UnusedIstreamPtr(ctx)).Steal();
    chunked->SetHandler(*ctx);
    pool_unref(pool);

    chunked->Read();
    chunked->Close();

    pool_commit();
}

#include "t_istream_filter.hxx"
