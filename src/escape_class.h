/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ESCAPE_CLASS_H
#define BENG_PROXY_ESCAPE_CLASS_H

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

static inline const char *
unescape_find(const struct escape_class *class, const char *p, size_t length)
{
    assert(class != NULL);
    assert(class->unescape_find != NULL);
    assert(p != NULL);

    return class->unescape_find(p, length);
}

static inline size_t
unescape_buffer(const struct escape_class *class, const char *p, size_t length,
                char *q)
{
    assert(class != NULL);
    assert(class->unescape != NULL);
    assert(p != NULL);
    assert(q != NULL);

    size_t length2 = class->unescape(p, length, q);
    assert(length2 <= length);

    return length2;
}

static inline size_t
unescape_inplace(const struct escape_class *class, char *p, size_t length)
{
    assert(class != NULL);
    assert(class->unescape != NULL);

    size_t length2 = class->unescape(p, length, p);
    assert(length2 <= length);

    return length2;
}

static inline const char *
escape_find(const struct escape_class *class, const char *p, size_t length)
{
    assert(class != NULL);
    assert(class->escape_find != NULL);

    return class->escape_find(p, length);
}

static inline size_t
escape_size(const struct escape_class *class, const char *p, size_t length)
{
    assert(class != NULL);
    assert(class->escape_size != NULL);

    return class->escape_size(p, length);
}

static inline const char *
escape_char(const struct escape_class *class, char ch)
{
    assert(class != NULL);
    assert(class->escape_char != NULL);

    const char *q = class->escape_char(ch);
    assert(q != NULL);
    return q;
}

static inline size_t
escape_buffer(const struct escape_class *class, const char *p, size_t length,
              char *q)
{
    assert(class != NULL);
    assert(class->escape != NULL);
    assert(p != NULL);
    assert(q != NULL);

    size_t length2 = class->escape(p, length, q);
    assert(length2 >= length);

    return length2;
}

#endif
