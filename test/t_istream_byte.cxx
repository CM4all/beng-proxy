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
#include "istream/ByteIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamByteTestTraits {
public:
    static constexpr const char *expected_result = "foo";

    static constexpr bool call_available = true;
    static constexpr bool got_data_assert = true;
    static constexpr bool enable_blocking = true;
    static constexpr bool enable_abort_istream = true;

    UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
        return istream_string_new(pool, "foo");
    }

    UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
                                UnusedIstreamPtr input) const noexcept {
        return istream_byte_new(pool, std::move(input));
    }
};

INSTANTIATE_TYPED_TEST_CASE_P(Byte, IstreamFilterTest,
                              IstreamByteTestTraits);
