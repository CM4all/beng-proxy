/*
 * Cookie management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie.h"
#include "strutil.h"
#include "strref.h"

#include <string.h>

static struct cookie *
cookie_list_find(struct list_head *head, const char *name, size_t name_length)
{
    struct cookie *cookie;

    for (cookie = (struct cookie *)head->next;
         &cookie->siblings != head;
         cookie = (struct cookie *)cookie->siblings.next)
        if (strref_cmp(&cookie->name, name, name_length) == 0)
            return cookie;

    return NULL;
}

static attr_always_inline void
ltrim(struct strref *s)
{
    while (s->length > 0 && char_is_whitespace(s->data[0])) {
        ++s->data;
        --s->length;
    }
}

static attr_always_inline void
rtrim(struct strref *s)
{
    while (s->length > 0 && char_is_whitespace(strref_last(s)))
        --s->length;
}

static attr_always_inline void
trim(struct strref *s)
{
    ltrim(s);
    rtrim(s);
}

static void
parse_cookie2(pool_t pool, struct list_head *head,
              const char *input, size_t input_length)
{
    const char *equals;
    struct strref name, value;
    struct cookie *cookie;

    equals = memchr(input, '=', input_length);
    if (equals == NULL)
        return;

    strref_set(&name, input, equals - input);
    trim(&name);
    if (strref_is_empty(&name))
        return;

    if (strref_cmp_literal(&name, "expires") == 0 ||
        strref_cmp_literal(&name, "domain") == 0 ||
        strref_cmp_literal(&name, "path") == 0)
        return; /* XXX */

    strref_set(&value, equals + 1, input + input_length - equals - 1);
    trim(&value);

    cookie = cookie_list_find(head, name.data, name.length);
    if (cookie == NULL) {
        if (strref_is_empty(&value))
            return;

        cookie = p_malloc(pool, sizeof(*cookie));
        strref_set_dup(pool, &cookie->name, &name);
        cookie->valid_until = (time_t)-1; /* XXX */

        list_add(&cookie->siblings, head);
    }

    strref_set_dup(pool, &cookie->value, &value);
}

void
cookie_list_set_cookie2(pool_t pool, struct list_head *head, const char *value)
{
    const char *semicolon;

    while ((semicolon = strchr(value, ';')) != NULL) {
        parse_cookie2(pool, head, value, semicolon - value);
        value = semicolon + 1;
    }

    if (*value != 0)
        parse_cookie2(pool, head, value, strlen(value));

    /* XXX: use "expires" and "path" arguments */
}

void
cookie_list_http_header(growing_buffer_t gb, struct list_head *head)
{
    struct cookie *cookie;

    if (list_empty(head))
        return;

    growing_buffer_write_string(gb, "Cookie2: $Version=\"1\"\r\nCookie: ");

    for (cookie = (struct cookie *)head->next;
         &cookie->siblings != head;
         cookie = (struct cookie *)cookie->siblings.next) {
        growing_buffer_write_buffer(gb, cookie->name.data, cookie->name.length);
        growing_buffer_write_string(gb, "=");
        /* XXX escape? */
        growing_buffer_write_buffer(gb, cookie->value.data, cookie->value.length);
        growing_buffer_write_string(gb, "; ");
    }

    growing_buffer_write_string(gb, "\r\n");
}
