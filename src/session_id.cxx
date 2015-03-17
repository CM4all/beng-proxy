/*
 * The session id data structure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_id.hxx"
#include "format.h"

#include <glib.h>

#include <stdlib.h>

bool
session_id_parse(const char *p, SessionId *id_r)
{
#ifdef SESSION_ID_WORDS
    char segment[9];
    SessionId id;
    char *endptr;

    if (strlen(p) != SESSION_ID_WORDS * 8)
        return false;

    segment[8] = 0;
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i) {
        memcpy(segment, p + i * 8, 8);
        id.data[i] = strtoul(segment, &endptr, 16);
        if (endptr != segment + 8)
            return false;
    }

    *id_r = id;
#else
    guint64 id;
    char *endptr;

    id = g_ascii_strtoull(p, &endptr, 16);
    if (id == 0 || *endptr != 0)
        return false;

    *id_r = (SessionId)id;
#endif

    return true;
}

const char *
session_id_format(SessionId id, struct session_id_string *string)
{
#ifdef SESSION_ID_WORDS
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        format_uint32_hex_fixed(string->buffer + i * 8, id.data[i]);
#else
    format_uint64_hex_fixed(string->buffer, id.value);
#endif
    string->buffer[sizeof(string->buffer) - 1] = 0;
    return string->buffer;
}
