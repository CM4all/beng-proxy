/*
 * Escape or unescape HTML entities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_html.hxx"
#include "escape_class.hxx"
#include "util/CharUtil.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

gcc_pure
static const char *
html_unescape_find(StringView p)
{
    return p.Find('&');
}

gcc_pure
static const char *
find_semicolon(const char *p, const char *end)
{
    while (p < end) {
        if (*p == ';')
            return p;
        else if (!IsAlphaASCII(*p))
            break;

        ++p;
    }

    return nullptr;
}

static size_t
html_unescape(StringView _p, char *q)
{
    const char *p = _p.begin(), *const p_end = _p.end(), *const q_start = q;

    const char *amp;
    while ((amp = (const char *)memchr(p, '&', p_end - p)) != nullptr) {
        memmove(q, p, amp - p);
        q += amp - p;

        StringView entity;
        entity.data = amp + 1;

        const char *semicolon = find_semicolon(entity.data, p_end);
        if (semicolon == nullptr) {
            *q++ = '&';
            p = amp + 1;
            continue;
        }

        entity.size = semicolon - entity.data;

        if (entity.EqualsLiteral("amp"))
            *q++ = '&';
        else if (entity.EqualsLiteral("quot"))
            *q++ = '"';
        else if (entity.EqualsLiteral("lt"))
            *q++ = '<';
        else if (entity.EqualsLiteral("gt"))
            *q++ = '>';
        else if (entity.EqualsLiteral("apos"))
            *q++ = '\'';

        p = semicolon + 1;
    }

    memmove(q, p, p_end - p);
    q += p_end - p;

    return q - q_start;
}

static size_t
html_escape_size(StringView _p)
{
    const char *p = _p.begin(), *const end = _p.end();

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
html_escape_find(StringView _p)
{
    const char *p = _p.begin(), *const end = _p.end();

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

    return nullptr;
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
        return nullptr;
    }
}

static size_t
html_escape(StringView _p, char *q)
{
    const char *p = _p.begin(), *const p_end = _p.end(), *const q_start = q;

    while (p < p_end) {
        char ch = *p++;
        switch (ch) {
        case '&':
            q = (char *)mempcpy(q, "&amp;", 5);
            break;

        case '"':
            q = (char *)mempcpy(q, "&quot;", 6);
            break;

        case '\'':
            q = (char *)mempcpy(q, "&apos;", 6);
            break;

        case '<':
            q = (char *)mempcpy(q, "&lt;", 4);
            break;

        case '>':
            q = (char *)mempcpy(q, "&gt;", 4);
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
