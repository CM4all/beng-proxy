/*
 * The session id data structure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_id.hxx"
#include "format.h"

#include <stdlib.h>

bool
SessionId::Parse(const char *p)
{
#ifdef SESSION_ID_SIZE
    if (strlen(p) != SESSION_ID_WORDS * 8)
        return false;

    char segment[9];
    segment[8] = 0;
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i) {
        memcpy(segment, p + i * 8, 8);
        char *endptr;
        data[i] = strtoul(segment, &endptr, 16);
        if (endptr != segment + 8)
            return false;
    }
#else
    char *endptr;
    value = strtoull(p, &endptr, 16);
    if (value == 0 || *endptr != 0)
        return false;
#endif

    return true;
}

const char *
SessionId::Format(struct session_id_string &string) const
{
#ifdef SESSION_ID_SIZE
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        format_uint32_hex_fixed(string.buffer + i * 8, data[i]);
#else
    format_uint64_hex_fixed(string.buffer, value);
#endif
    string.buffer[sizeof(string.buffer) - 1] = 0;
    return string.buffer;
}
