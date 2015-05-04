/*
 * Escape or unescape CSS strings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_css.hxx"
#include "escape_class.h"
#include "strref.h"

#include <assert.h>
#include <string.h>

static const char *
css_unescape_find(const char *p, size_t length)
{
    return (const char *)memchr(p, '\\', length);
}

static constexpr bool
need_simple_escape(char ch)
{
    return ch == '\\' || ch == '"' || ch == '\'';
}

static size_t
css_unescape(const char *p, size_t length, char *q)
{
    const char *p_end = p + length, *q_start = q;

    const char *bs;
    while ((bs = (const char *)memchr(p, '\\', p_end - p)) != nullptr) {
        memmove(q, p, bs - p);
        q += bs - p;

        p = bs + 1;

        if (p < p_end && need_simple_escape(*p))
            *q++ = *p++;
        else
            /* XXX implement newline and hex codes */
            *q++ = '\\';
    }

    memmove(q, p, p_end - p);
    q += p_end - p;

    return q - q_start;
}

static size_t
css_escape_size(const char *p, size_t length)
{
    const char *end = p + length;

    size_t size = 0;
    while (p < end) {
        if (need_simple_escape(*p))
            size += 2;
        else
            /* XXX implement newline and hex codes */
            ++size;
    }

    return size;
}

static const char *
css_escape_find(const char *p, size_t length)
{
    const char *end = p + length;

    while (p < end) {
        if (need_simple_escape(*p))
            return p;

        ++p;
    }

    return nullptr;
}

static const char *
css_escape_char(char ch)
{
    switch (ch) {
    case '\\':
        return "\\\\";

    case '"':
        return "\\\"";

    case '\'':
        return "\\'";

    default:
        assert(false);
        return nullptr;
    }
}

static size_t
css_escape(const char *p, size_t length, char *q)
{
    const char *p_end = p + length, *q_start = q;

    while (p < p_end) {
        char ch = *p++;
        if (need_simple_escape(ch)) {
            *q++ = '\\';
            *q++ = ch;
        } else
            *q++ = ch;
    }

    return q - q_start;
}

const struct escape_class css_escape_class = {
    .unescape_find = css_unescape_find,
    .unescape = css_unescape,
    .escape_find = css_escape_find,
    .escape_char = css_escape_char,
    .escape_size = css_escape_size,
    .escape = css_escape,
};
