/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "pexpand.hxx"
#include "expand.hxx"
#include "AllocatorPtr.hxx"
#include "regex.hxx"
#include "uri/Unescape.hxx"
#include "pcre/MatchInfo.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

const char *
expand_string(AllocatorPtr alloc, const char *src,
              const MatchInfo &match_info)
{
    assert(src != nullptr);
    assert(match_info.IsDefined());

    const size_t length = ExpandStringLength(src, match_info);
    const auto buffer = alloc.NewArray<char>(length + 1);

    struct Result {
        char *q;

        explicit Result(char *_q):q(_q) {}

        void Append(char ch) {
            *q++ = ch;
        }

        void Append(const char *p) {
            q = stpcpy(q, p);
        }

        void Append(const char *p, size_t _length) {
            q = (char *)mempcpy(q, p, _length);
        }

        void AppendValue(const char *p, size_t _length) {
            Append(p, _length);
        }
    };

    Result result(buffer);
    ExpandString(result, src, match_info);

    assert(result.q == buffer + length);
    *result.q = 0;

    return buffer;
}

const char *
expand_string_unescaped(AllocatorPtr alloc, const char *src,
                        const MatchInfo &match_info)
{
    assert(src != nullptr);
    assert(match_info.IsDefined());

    const size_t length = ExpandStringLength(src, match_info);
    const auto buffer = alloc.NewArray<char>(length + 1);

    struct Result {
        char *q;

        explicit Result(char *_q):q(_q) {}

        void Append(char ch) {
            *q++ = ch;
        }

        void Append(const char *p) {
            q = stpcpy(q, p);
        }

        void Append(const char *p, size_t _length) {
            q = (char *)mempcpy(q, p, _length);
        }

        void AppendValue(const char *p, size_t _length) {
            q = UriUnescape(q, {p, _length});
            if (q == nullptr)
                throw std::runtime_error("Malformed URI escape");
        }
    };

    Result result(buffer);
    ExpandString(result, src, match_info);

    assert(result.q <= buffer + length);
    *result.q = 0;

    return buffer;
}
