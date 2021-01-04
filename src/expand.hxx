/*
 * Copyright 2007-2021 CM4all GmbH
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

#ifndef BENG_PROXY_EXPAND_HXX
#define BENG_PROXY_EXPAND_HXX

#include "util/RuntimeError.hxx"

#include "util/Compiler.h"

#include <stdexcept>

#include <assert.h>
#include <string.h>

/**
 * Throws std::runtime_error on error.
 */
template<typename Result, typename MatchInfo>
void
ExpandString(Result &result, const char *src, MatchInfo &&match_info)
{
    assert(src != nullptr);

    while (true) {
        const char *backslash = strchr(src, '\\');
        if (backslash == nullptr) {
            /* append the remaining input string and return */
            result.Append(src);
            return;
        }

        /* copy everything up to the backslash */
        result.Append(src, backslash - src);

        /* now evaluate the escape */
        src = backslash + 1;
        const char ch = *src++;
        if (ch == '\\')
            result.Append(ch);
        else if (ch >= '0' && ch <= '9') {
            auto c = match_info.GetCapture(ch - '0');
            if (c.IsNull())
                throw std::runtime_error("Invalid regex capture");

            if (!c.empty())
                result.AppendValue(c.data, c.size);
        } else {
            throw FormatRuntimeError("Invalid backslash escape (0x%02x)", ch);
        }
    }
}

#endif
