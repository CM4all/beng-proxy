/*
 * The session id data structure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_ID_H
#define BENG_PROXY_SESSION_ID_H

#include <inline/compiler.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef SESSION_ID_SIZE
#include <string.h> /* for memcmp() */
#endif

#ifdef SESSION_ID_SIZE
#define SESSION_ID_WORDS (((SESSION_ID_SIZE) + 1) / 4)

typedef struct {
    uint32_t data[SESSION_ID_WORDS];
} session_id_t;

#else

typedef uint64_t session_id_t;

#endif

/**
 * Buffer for the function session_id_format().
 */
struct session_id_string {
    /**
     * Two hex characters per byte, plus the terminating zero.
     */
    char buffer[sizeof(session_id_t) * 2 + 1];
};

gcc_pure
static inline bool
session_id_is_defined(session_id_t id)
{
#ifdef SESSION_ID_WORDS
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        if (id.data[i] != 0)
            return true;
    return false;
#else
    return id != 0;
#endif
}

static inline void
session_id_clear(session_id_t *id_p)
{
#ifdef SESSION_ID_WORDS
    memset(id_p, 0, sizeof(*id_p));
#else
    *id_p = 0;
#endif
}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a session id from a string.
 *
 * @return true on success, false on error
 */
bool
session_id_parse(const char *p, session_id_t *id_r);

const char *
session_id_format(session_id_t id, struct session_id_string *string);

#ifdef __cplusplus
}
#endif

static inline unsigned
session_id_low(session_id_t id)
{
#ifdef SESSION_ID_WORDS
    return id.data[0];
#else
    return (unsigned)id;
#endif
}

gcc_const
static inline bool
session_id_equals(const session_id_t a, const session_id_t b)
{
#ifdef SESSION_ID_WORDS
    return memcmp(&a, &b, sizeof(a)) == 0;
#else
    return a == b;
#endif
}

#endif
