/*
 * The session id data structure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_ID_H
#define BENG_PROXY_SESSION_ID_H

#include <inline/compiler.h>

#include <stdint.h>

#ifdef SESSION_ID_SIZE
#include <array>
#include <string.h> /* for memcmp() */
#endif

#ifdef SESSION_ID_SIZE
#define SESSION_ID_WORDS (((SESSION_ID_SIZE) + 1) / 4)
#endif

struct SessionId {
#ifdef SESSION_ID_SIZE
    std::array<uint32_t, SESSION_ID_WORDS> data;
#else
    uint64_t value;

    SessionId() = default;

    explicit constexpr SessionId(uint64_t _value):value(_value) {}
#endif
};

/**
 * Buffer for the function session_id_format().
 */
struct session_id_string {
    /**
     * Two hex characters per byte, plus the terminating zero.
     */
    char buffer[sizeof(SessionId) * 2 + 1];
};

gcc_pure
static inline bool
session_id_is_defined(SessionId id)
{
#ifdef SESSION_ID_WORDS
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        if (id.data[i] != 0)
            return true;
    return false;
#else
    return id.value != 0;
#endif
}

static inline void
session_id_clear(SessionId *id_p)
{
#ifdef SESSION_ID_WORDS
    memset(id_p, 0, sizeof(*id_p));
#else
    id_p->value = 0;
#endif
}

/**
 * Parse a session id from a string.
 *
 * @return true on success, false on error
 */
bool
session_id_parse(const char *p, SessionId *id_r);

const char *
session_id_format(SessionId id, struct session_id_string *string);

static inline unsigned
session_id_low(SessionId id)
{
#ifdef SESSION_ID_WORDS
    return id.data[0];
#else
    return id.value;
#endif
}

gcc_const
static inline bool
session_id_equals(const SessionId a, const SessionId b)
{
#ifdef SESSION_ID_WORDS
    return memcmp(&a, &b, sizeof(a)) == 0;
#else
    return a.value == b.value;
#endif
}

#endif
