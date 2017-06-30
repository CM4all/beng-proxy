/*
 * author: Max Kellermann <mk@cm4all.com>
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

            if (!c.IsEmpty())
                result.AppendValue(c.data, c.size);
        } else {
            throw FormatRuntimeError("Invalid backslash escape (0x%02x)", ch);
        }
    }
}

#endif
