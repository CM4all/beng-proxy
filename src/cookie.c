/*
 * Cookie management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie.h"
#include "strutil.h"

#include <assert.h>
#include <string.h>

static struct cookie *
cookie_list_find(struct list_head *head, const char *name)
{
    struct cookie *cookie;

    for (cookie = (struct cookie *)head->next;
         &cookie->siblings != head;
         cookie = (struct cookie *)cookie->siblings.next)
        if (strcmp(cookie->name, name) == 0)
            return cookie;

    return NULL;
}

static attr_always_inline void
ltrim(const char **p_r, size_t *length_r)
{
    while (*length_r > 0 && char_is_whitespace(**p_r)) {
        ++(*p_r);
        --(*length_r);
    }
}

static attr_always_inline void
rtrim(const char **p_r, size_t *length_r)
{
    while (*length_r > 0 && char_is_whitespace((*p_r)[*length_r - 1]))
        --(*length_r);
}

static attr_always_inline void
trim(const char **p_r, size_t *length_r)
{
    ltrim(p_r, length_r);
    rtrim(p_r, length_r);
}

static void
parse_cookie2(pool_t pool, struct list_head *head,
              const char *input, size_t input_length)
{
    const char *equals, *name, *value;
    size_t name_length, value_length;
    struct cookie *cookie;

    equals = memchr(input, '=', input_length);
    if (equals == NULL)
        return;

    name = input;
    name_length = equals - input;
    trim(&name, &name_length);
    if (name_length == 0)
        return;

    if ((name_length == 7 && strncasecmp(name, "expires", 7) == 0) ||
        (name_length == 6 && strncasecmp(name, "domain", 6) == 0) ||
        (name_length == 4 && strncasecmp(name, "path", 4) == 0))
        return; /* XXX */

    value = equals + 1;
    value_length = input + input_length - value;
    trim(&value, &value_length);

    name = p_strndup(pool, name, name_length);
    value = value_length == 0 ? NULL : p_strndup(pool, value, value_length);

    cookie = cookie_list_find(head, name);
    if (cookie == NULL) {
        if (value == NULL)
            return;

        cookie = p_malloc(pool, sizeof(*cookie));
        cookie->name = name;
        cookie->valid_until = (time_t)-1; /* XXX */

        list_add(&cookie->siblings, head);
    }

    cookie->value = value;
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
        growing_buffer_write_string(gb, cookie->name);
        growing_buffer_write_string(gb, "=");
        /* XXX escape? */
        growing_buffer_write_string(gb, cookie->value);
        growing_buffer_write_string(gb, "; ");
    }

    growing_buffer_write_string(gb, "\r\n");
}
