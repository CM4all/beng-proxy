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

#include "Compare.hxx"
#include "util/HexParse.hxx"

#include <string.h>

const char *
UriFindUnescapedSuffix(const char *const uri_start,
                       const char *const suffix_start) noexcept
{
    const char *uri_i = (const char *)rawmemchr(uri_start, '\0');
    const char *suffix_i = (const char *)rawmemchr(suffix_start, '\0');

    while (true) {
        if (suffix_i == suffix_start)
            /* full match - success */
            return uri_i;

        if (uri_i == uri_start)
            /* URI is too short - fail */
            return nullptr;

        --uri_i;
        --suffix_i;

        char suffix_ch = *suffix_i;

        if (suffix_ch == '%')
            /* malformed escape */
            return nullptr;

        if (suffix_start + 2 <= suffix_i &&
            suffix_i[-2] == '%') {
            const int digit1 = ParseHexDigit(suffix_ch);
            if (digit1 < 0)
                /* malformed hex digit */
                return nullptr;

            const int digit2 = ParseHexDigit(*--suffix_i);
            if (digit2 < 0)
                /* malformed hex digit */
                return nullptr;

            --suffix_i;
            suffix_ch = (digit2 << 4) | digit1;
        }

        if (*uri_i != suffix_ch)
            /* mismatch */
            return nullptr;
    }
}
