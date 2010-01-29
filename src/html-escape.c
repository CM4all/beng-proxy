/*
 * Escape or unescape HTML entities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "html-escape.h"
#include "strref.h"

#include <assert.h>
#include <string.h>

static char buffer[1024];

static size_t
copy_to_buffer(size_t pos, const char *src, size_t length)
{
    size_t space = sizeof(buffer) - pos;

    if (length > space)
        length = space;

    memcpy(buffer + pos, src, length);
    return length;
}

struct unescape {
    const char *first_unconsumed_src;
    size_t dest_pos;
};

static size_t
copy_to_buffer2(struct unescape *u, const char *up_to_src)
{
    size_t nbytes;

    assert(u != NULL);
    assert(u->dest_pos <= sizeof(buffer));
    assert(up_to_src != NULL);
    assert(up_to_src >= u->first_unconsumed_src);

    nbytes = copy_to_buffer(u->dest_pos, u->first_unconsumed_src,
                            up_to_src - u->first_unconsumed_src);
    u->first_unconsumed_src = up_to_src;
    u->dest_pos += nbytes;

    assert(u->dest_pos <= sizeof(buffer));

    return nbytes;
}

static size_t
rest_to_buffer(struct unescape *u, const char *up_to_src, const char *first,
               struct strref *s)
{
    if (u->dest_pos == 0)
        return 0;

    copy_to_buffer2(u, up_to_src);

    s->length = u->dest_pos;
    s->data = buffer;

    return u->first_unconsumed_src - first;
}

static void
replace(struct unescape *u, const char *start_src, const char *after_src,
        const char *value, size_t length)
{
    assert(start_src != NULL);
    assert(after_src != NULL);
    assert(after_src > start_src);

    copy_to_buffer2(u, start_src);
    if (u->dest_pos + length < sizeof(buffer)) {
        memcpy(buffer + u->dest_pos, value, length);
        u->dest_pos += length;
        u->first_unconsumed_src = after_src;
    }
}

size_t
html_unescape(struct strref *s)
{
    struct unescape u = {
        .first_unconsumed_src = s->data,
        .dest_pos = 0,
    };
    const char *end = s->data + s->length;
    const char *cursor = s->data, *p, *q, *semicolon;
    struct strref entity;

    q = memchr(cursor, '&', end - cursor);
    if (q == NULL)
        return 0;

    while (q != NULL && u.dest_pos < sizeof(buffer)) {
        p = q;
        q = memchr(p + 1, '&', end - p - 1);

        entity.data = p + 1;

        semicolon = memchr(entity.data, ';', end - entity.data);
        if (semicolon == NULL)
            break;

        entity.length = semicolon - entity.data;

        if (strref_cmp_literal(&entity, "amp") == 0)
            replace(&u, p, semicolon + 1, "&", 1);
        else if (strref_cmp_literal(&entity, "quot") == 0)
            replace(&u, p, semicolon + 1, "\"", 1);
        else if (strref_cmp_literal(&entity, "lt") == 0)
            replace(&u, p, semicolon + 1, "<", 1);
        else if (strref_cmp_literal(&entity, "gt") == 0)
            replace(&u, p, semicolon + 1, ">", 1);
        else if (strref_cmp_literal(&entity, "apos") == 0)
            replace(&u, p, semicolon + 1, "'", 1);

        cursor = semicolon + 1;
    }

    return rest_to_buffer(&u, end, s->data, s);
}

size_t
html_unescape_inplace(char *p, size_t length)
{
    const char *end = p + length;
    char *cursor = p, *ampersand, *next, *semicolon;
    struct strref entity;

    while (cursor < end) {
        ampersand = memchr(cursor, '&', end - cursor);
        if (ampersand == NULL)
            break;

        entity.data = ampersand + 1;

        semicolon = memchr(entity.data, ';', end - entity.data);
        if (semicolon == NULL)
            break;

        next = memchr(entity.data, '&', semicolon - entity.data);
        if (next != NULL) {
            /* try to handle nested entity gracecfully */
            cursor = next;
            continue;
        }

        entity.length = semicolon - entity.data;

        cursor = ampersand;
        if (strref_cmp_literal(&entity, "amp") == 0) {
            *cursor++ = '&';
            end -= 4;
            memmove(cursor, cursor + 4, end - cursor);
        } else if (strref_cmp_literal(&entity, "quot") == 0) {
            *cursor++ = '"';
            end -= 5;
            memmove(cursor, cursor + 5, end - cursor);
        } else if (strref_cmp_literal(&entity, "apos") == 0) {
            *cursor++ = '\'';
            end -= 5;
            memmove(cursor, cursor + 5, end - cursor);
        } else if (strref_cmp_literal(&entity, "lt") == 0) {
            *cursor++ = '<';
            end -= 3;
            memmove(cursor, cursor + 3, end - cursor);
        } else if (strref_cmp_literal(&entity, "gt") == 0) {
            *cursor++ = '>';
            end -= 3;
            memmove(cursor, cursor + 3, end - cursor);
        } else
            cursor = semicolon + 1;
    }

    return end - p;
}

size_t
html_escape(struct strref *s)
{
    struct unescape u = {
        .first_unconsumed_src = s->data,
        .dest_pos = 0,
    };

    const char *end = s->data + s->length;
    const char *cursor;

    for (cursor = s->data; cursor < end; ++cursor) {
        switch (*cursor) {
        case '&':
            replace(&u, cursor, cursor + 1, "&amp;", 5);
            break;

        case '<':
            replace(&u, cursor, cursor + 1, "&lt;", 4);
            break;

        case '>':
            replace(&u, cursor, cursor + 1, "&gt;", 4);
            break;

        case '"':
            replace(&u, cursor, cursor + 1, "&quot;", 6);
            break;

        case '\'':
            replace(&u, cursor, cursor + 1, "&apos;", 6);
            break;
        }
    }

    return rest_to_buffer(&u, end, s->data, s);
}
