/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_EXPAND_HXX
#define BENG_PROXY_EXPAND_HXX

#include "util/Error.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

class Domain;
extern const Domain expand_domain;

template<typename Result, typename MatchInfo>
bool
ExpandString(Result &result, const char *src,
             MatchInfo &&match_info, Error &error)
{
    assert(src != nullptr);

    while (true) {
        const char *backslash = strchr(src, '\\');
        if (backslash == nullptr) {
            /* append the remaining input string and return */
            result.Append(src);
            return true;
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
            if (!c.IsEmpty())
                result.AppendValue(c.data, c.size);
        } else {
            error.Format(expand_domain,
                         "Invalid backslash escape (0x%02x)", ch);
            return false;
        }
    }
}

#endif
