/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ESCAPE_CLASS_HXX
#define BENG_PROXY_ESCAPE_CLASS_HXX

#include "util/StringView.hxx"

#include "util/Compiler.h"

#include <assert.h>
#include <stddef.h>

struct escape_class {
    /**
     * Find the first character that must be unescaped.  Returns nullptr
     * when the string can be used as-is without unescaping.
     */
    const char *(*unescape_find)(StringView p);

    /**
     * Unescape the given string into the output buffer.  Returns the
     * number of characters in the output buffer.
     */
    size_t (*unescape)(StringView p, char *q);

    /**
     * Find the first character that must be escaped.  Returns nullptr
     * when there are no such characters.
     */
    const char *(*escape_find)(StringView p);

    /**
     * Returns the escape string for the specified character.
     */
    const char *(*escape_char)(char ch);

    /**
     * Measure the minimum buffer size for escaping the given string.
     * Returns 0 when no escaping is needed.
     */
    size_t (*escape_size)(StringView p);

    /**
     * Escape the given string into the output buffer.  Returns the
     * number of characters in the output buffer.
     */
    size_t (*escape)(StringView p, char *q);
};

gcc_pure
static inline const char *
unescape_find(const struct escape_class *cls, StringView p)
{
    assert(cls != nullptr);
    assert(cls->unescape_find != nullptr);

    return cls->unescape_find(p);
}

static inline size_t
unescape_buffer(const struct escape_class *cls, StringView p, char *q)
{
    assert(cls != nullptr);
    assert(cls->unescape != nullptr);
    assert(q != nullptr);

    size_t length2 = cls->unescape(p, q);
    assert(length2 <= p.size);

    return length2;
}

static inline size_t
unescape_inplace(const struct escape_class *cls, char *p, size_t length)
{
    assert(cls != nullptr);
    assert(cls->unescape != nullptr);

    size_t length2 = cls->unescape({p, length}, p);
    assert(length2 <= length);

    return length2;
}

gcc_pure
static inline const char *
escape_find(const struct escape_class *cls, StringView p)
{
    assert(cls != nullptr);
    assert(cls->escape_find != nullptr);

    return cls->escape_find(p);
}

gcc_pure
static inline size_t
escape_size(const struct escape_class *cls, StringView p)
{
    assert(cls != nullptr);
    assert(cls->escape_size != nullptr);

    return cls->escape_size(p);
}

gcc_pure
static inline const char *
escape_char(const struct escape_class *cls, char ch)
{
    assert(cls != nullptr);
    assert(cls->escape_char != nullptr);

    const char *q = cls->escape_char(ch);
    assert(q != nullptr);
    return q;
}

static inline size_t
escape_buffer(const struct escape_class *cls, StringView p, char *q)
{
    assert(cls != nullptr);
    assert(cls->escape != nullptr);
    assert(q != nullptr);

    size_t length2 = cls->escape(p, q);
    assert(length2 >= p.size);

    return length2;
}

#endif
