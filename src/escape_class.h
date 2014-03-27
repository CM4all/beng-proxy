/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ESCAPE_CLASS_H
#define BENG_PROXY_ESCAPE_CLASS_H

#include <inline/compiler.h>

#include <assert.h>
#include <stddef.h>

struct escape_class {
    /**
     * Find the first character that must be unescaped.  Returns NULL
     * when the string can be used as-is without unescaping.
     */
    const char *(*unescape_find)(const char *p, size_t length);

    /**
     * Unescape the given string into the output buffer.  Returns the
     * number of characters in the output buffer.
     */
    size_t (*unescape)(const char *p, size_t length, char *q);

    /**
     * Find the first character that must be escaped.  Returns NULL
     * when there are no such characters.
     */
    const char *(*escape_find)(const char *p, size_t length);

    /**
     * Returns the escape string for the specified character.
     */
    const char *(*escape_char)(char ch);

    /**
     * Measure the minimum buffer size for escaping the given string.
     * Returns 0 when no escaping is needed.
     */
    size_t (*escape_size)(const char *p, size_t length);

    /**
     * Escape the given string into the output buffer.  Returns the
     * number of characters in the output buffer.
     */
    size_t (*escape)(const char *p, size_t length, char *q);
};

gcc_pure
static inline const char *
unescape_find(const struct escape_class *cls, const char *p, size_t length)
{
    assert(cls != NULL);
    assert(cls->unescape_find != NULL);
    assert(p != NULL);

    return cls->unescape_find(p, length);
}

static inline size_t
unescape_buffer(const struct escape_class *cls, const char *p, size_t length,
                char *q)
{
    assert(cls != NULL);
    assert(cls->unescape != NULL);
    assert(p != NULL);
    assert(q != NULL);

    size_t length2 = cls->unescape(p, length, q);
    assert(length2 <= length);

    return length2;
}

static inline size_t
unescape_inplace(const struct escape_class *cls, char *p, size_t length)
{
    assert(cls != NULL);
    assert(cls->unescape != NULL);

    size_t length2 = cls->unescape(p, length, p);
    assert(length2 <= length);

    return length2;
}

gcc_pure
static inline const char *
escape_find(const struct escape_class *cls, const char *p, size_t length)
{
    assert(cls != NULL);
    assert(cls->escape_find != NULL);

    return cls->escape_find(p, length);
}

gcc_pure
static inline size_t
escape_size(const struct escape_class *cls, const char *p, size_t length)
{
    assert(cls != NULL);
    assert(cls->escape_size != NULL);

    return cls->escape_size(p, length);
}

gcc_pure
static inline const char *
escape_char(const struct escape_class *cls, char ch)
{
    assert(cls != NULL);
    assert(cls->escape_char != NULL);

    const char *q = cls->escape_char(ch);
    assert(q != NULL);
    return q;
}

static inline size_t
escape_buffer(const struct escape_class *cls, const char *p, size_t length,
              char *q)
{
    assert(cls != NULL);
    assert(cls->escape != NULL);
    assert(p != NULL);
    assert(q != NULL);

    size_t length2 = cls->escape(p, length, q);
    assert(length2 >= length);

    return length2;
}

#endif
