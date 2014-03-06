/*
 * Escape or unescape HTML entities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_html.h"
#include "escape_class.h"
#include "strref.h"
#include "strutil.h"

#include <assert.h>
#include <string.h>

static const char *
html_unescape_find(const char *p, size_t length)
{
    return memchr(p, '&', length);
}

static const char *
find_semicolon(const char *p, const char *end)
{
    while (p < end) {
        if (*p == ';')
            return p;
        else if (!char_is_letter(*p))
            break;

        ++p;
    }

    return NULL;
}

static size_t
html_unescape(const char *p, size_t length, char *q)
{
    const char *p_end = p + length, *q_start = q;

    const char *amp;
    while ((amp = memchr(p, '&', p_end - p)) != NULL) {
        memmove(q, p, amp - p);
        q += amp - p;

        struct strref entity;
        entity.data = amp + 1;

        const char *semicolon = find_semicolon(entity.data, p_end);
        if (semicolon == NULL) {
            *q++ = '&';
            p = amp + 1;
            continue;
        }

        entity.length = semicolon - entity.data;

        if (strref_cmp_literal(&entity, "amp") == 0)
            *q++ = '&';
        else if (strref_cmp_literal(&entity, "quot") == 0)
            *q++ = '"';
        else if (strref_cmp_literal(&entity, "lt") == 0)
            *q++ = '<';
        else if (strref_cmp_literal(&entity, "gt") == 0)
            *q++ = '>';
        else if (strref_cmp_literal(&entity, "apos") == 0)
            *q++ = '\'';

        p = semicolon + 1;
    }

    memmove(q, p, p_end - p);
    q += p_end - p;

    return q - q_start;
}

static size_t
html_escape_size(const char *p, size_t length)
{
    const char *end = p + length;

    size_t size = 0;
    while (p < end) {
        switch (*p++) {
        case '&':
            size += 5;
            break;

        case '"':
        case '\'':
            size += 6;
            break;

        case '<':
        case '>':
            size += 4;
            break;

        default:
            ++size;
        }
    }

    return size;
}

static const char *
html_escape_find(const char *p, size_t length)
{
    const char *end = p + length;

    while (p < end) {
        switch (*p) {
        case '&':
        case '"':
        case '\'':
        case '<':
        case '>':
            return p;

        default:
            ++p;
        }
    }

    return NULL;
}

static const char *
html_escape_char(char ch)
{
    switch (ch) {
    case '&':
        return "&amp;";

    case '"':
        return "&quot;";

    case '\'':
        return "&apos;";

    case '<':
        return "&lt;";

    case '>':
        return "&gt;";

    default:
        assert(false);
        return NULL;
    }
}

static size_t
html_escape(const char *p, size_t length, char *q)
{
    const char *p_end = p + length, *q_start = q;

    while (p < p_end) {
        char ch = *p++;
        switch (ch) {
        case '&':
            memmove(q, "&amp;", 5);
            q += 5;
            break;

        case '"':
            memmove(q, "&quot;", 6);
            q += 6;
            break;

        case '\'':
            memmove(q, "&apos;", 6);
            q += 6;
            break;

        case '<':
            memmove(q, "&lt;", 4);
            q += 4;
            break;

        case '>':
            memmove(q, "&gt;", 4);
            q += 4;
            break;

        default:
            *q++ = ch;
        }
    }

    return q - q_start;
}

const struct escape_class html_escape_class = {
    .unescape_find = html_unescape_find,
    .unescape = html_unescape,
    .escape_find = html_escape_find,
    .escape_char = html_escape_char,
    .escape_size = html_escape_size,
    .escape = html_escape,
};
