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

#include "IstreamFilterTest.hxx"
#include "istream/istream_catch.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/Exception.hxx"

#include <stdio.h>

static std::exception_ptr
catch_callback(std::exception_ptr ep, gcc_unused void *ctx)
{
    fprintf(stderr, "caught: %s\n", GetFullMessage(ep).c_str());
    return {};
}

class IstreamCatchTestTraits : public SkipErrorTraits {
public:
    /* an input string longer than the "space" buffer (128 bytes) to
       trigger bugs due to truncated OnData() buffers */
    static constexpr const char *expected_result =
        "long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long";

    static constexpr bool call_available = false;
    static constexpr bool got_data_assert = true;
    static constexpr bool enable_blocking = true;
    static constexpr bool enable_abort_istream = true;

    UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
        return istream_string_new(pool, expected_result);
    }

    UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
                                UnusedIstreamPtr input) const noexcept {
        return istream_catch_new(&pool, std::move(input),
                                 catch_callback, nullptr);
    }
};

INSTANTIATE_TYPED_TEST_CASE_P(Catch, IstreamFilterTest,
                              IstreamCatchTestTraits);
