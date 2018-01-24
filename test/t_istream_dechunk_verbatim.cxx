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

#include "istream/istream_dechunk.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"

#include <stdio.h>

#define EXPECTED_RESULT "3\r\nfoo\r\n0\r\n\r\n"

/* add space at the end so we don't run into an assertion failure when
   istream_string reports EOF but istream_dechunk has already cleared
   its handler */
#define INPUT EXPECTED_RESULT " "

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(*pool, INPUT).Steal();
}

class MyDechunkHandler final : public DechunkHandler {
    void OnDechunkEndSeen() override {}

    bool OnDechunkEnd() override {
        return false;
    }
};

static Istream *
create_test(EventLoop &event_loop, struct pool *pool, Istream *input)
{
    auto *handler = NewFromPool<MyDechunkHandler>(*pool);
    input = istream_dechunk_new(*pool, *input, event_loop, *handler);
    istream_dechunk_check_verbatim(*input);
#ifdef T_BYTE
    input = istream_byte_new(*pool, UnusedIstreamPtr(input)).Steal();
#endif
#ifdef T_FOUR
    input = istream_four_new(pool, *input);
#endif
    return input;
}

#include "t_istream_filter.hxx"
