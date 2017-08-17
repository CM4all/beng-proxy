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

#include "uri_escape.hxx"
#include "util/CharUtil.hxx"
#include "util/StringView.hxx"
#include "util/HexFormat.h"

#include <algorithm>

#include <string.h>

/**
 * @see RFC 3986 2.3
 */
constexpr
static inline bool
IsUriUnreserved(char ch)
{
    return IsAlphaNumericASCII(ch) ||
        ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

size_t
uri_escape(char *dest, StringView src,
           char escape_char)
{
    size_t dest_length = 0;

    for (size_t i = 0; i < src.size; ++i) {
        if (IsUriUnreserved(src[i])) {
            dest[dest_length++] = src[i];
        } else {
            dest[dest_length++] = escape_char;
            format_uint8_hex_fixed(&dest[dest_length], (uint8_t)src[i]);
            dest_length += 2;
        }
    }

    return dest_length;
}

size_t
uri_escape(char *dest, ConstBuffer<void> src,
           char escape_char)
{
    return uri_escape(dest, StringView((const char *)src.data, src.size),
                      escape_char);
}

static int
parse_hexdigit(char ch)
{
    if (IsDigitASCII(ch))
        return ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 0xa;
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 0xa;
    else
        return -1;
}

char *
uri_unescape(char *dest, StringView _src, char escape_char)
{
    auto src = _src.begin();
    const auto end = _src.end();

    while (true) {
        auto p = std::find(src, end, escape_char);
        dest = std::copy(src, p, dest);

        if (p == end)
            break;

        if (p >= end - 2)
            /* percent sign at the end of string */
            return nullptr;

        const int digit1 = parse_hexdigit(p[1]);
        const int digit2 = parse_hexdigit(p[2]);
        if (digit1 == -1 || digit2 == -1)
            /* invalid hex digits */
            return nullptr;

        const char ch = (char)((digit1 << 4) | digit2);
        if (ch == 0)
            /* no %00 hack allowed! */
            return nullptr;

        *dest++ = ch;
        src = p + 3;
    }

    return dest;
}
