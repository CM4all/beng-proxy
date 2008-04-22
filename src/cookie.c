/*
 * Cookie management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie.h"
#include "strutil.h"
#include "strref.h"
#include "strmap.h"
#include "http-string.h"
#include "tpool.h"

#include <string.h>

struct cookie {
    struct list_head siblings;

    struct strref name;
    struct strref value;
    const char *domain;
    time_t valid_until;
};

static bool
domain_matches(const char *domain, const char *match)
{
    /* XXX */

    return strcmp(domain, match) == 0;
}

static struct cookie *
cookie_list_find(struct list_head *head, const char *domain,
                 const char *name, size_t name_length)
{
    struct cookie *cookie;

    for (cookie = (struct cookie *)head->next;
         &cookie->siblings != head;
         cookie = (struct cookie *)cookie->siblings.next)
        if (domain_matches(domain, cookie->domain) &&
            strref_cmp(&cookie->name, name, name_length) == 0)
            return cookie;

    return NULL;
}

static __attr_always_inline void
ltrim(struct strref *s)
{
    while (s->length > 0 && char_is_whitespace(s->data[0])) {
        ++s->data;
        --s->length;
    }
}

static __attr_always_inline void
rtrim(struct strref *s)
{
    while (s->length > 0 && char_is_whitespace(strref_last(s)))
        --s->length;
}

static __attr_always_inline void
trim(struct strref *s)
{
    ltrim(s);
    rtrim(s);
}

static void
parse_key_value(pool_t pool, struct strref *input,
                struct strref *name, struct strref *value)
{
    http_next_token(input, name);
    if (strref_is_empty(name))
        return;

    ltrim(input);
    if (!strref_is_empty(input) && input->data[0] == '=') {
        strref_skip(input, 1);
        ltrim(input);
        http_next_value(pool, input, value);
    } else
        strref_clear(value);
}

static int
parse_next_cookie(struct cookie_jar *jar, struct strref *input,
                  const char *domain)
{
    struct strref name, value;
    struct cookie *cookie;

    parse_key_value(jar->pool, input, &name, &value);
    if (strref_is_empty(&name))
        return 0;

    cookie = cookie_list_find(&jar->cookies, domain, name.data, name.length);
    if (cookie == NULL) {
        cookie = p_malloc(jar->pool, sizeof(*cookie));

        /* XXX domain from cookie attribute */
        cookie->domain = p_strdup(jar->pool, domain);

        strref_set_dup(jar->pool, &cookie->name, &name);
        cookie->valid_until = (time_t)-1; /* XXX */

        list_add(&cookie->siblings, &jar->cookies);
    }

    strref_set_dup(jar->pool, &cookie->value, &value);

    ltrim(input);
    while (!strref_is_empty(input) && input->data[0] == ';') {
        strref_skip(input, 1);

        parse_key_value(jar->pool, input, &name, &value);
        if (!strref_is_empty(&name)) {
            /* XXX */
        }
    }

    /* XXX: use "expires" and "path" arguments */

    return 1;
}

void
cookie_jar_set_cookie2(struct cookie_jar *jar, const char *value,
                       const char *domain)
{
    struct strref input;

    strref_set_c(&input, value);

    while (1) {
        if (!parse_next_cookie(jar, &input, domain))
            break;

        if (strref_is_empty(&input))
            return;

        if (input.data[0] != ',')
            break;

        strref_skip(&input, 1);
        ltrim(&input);
    }

    /* XXX log error */
}

void
cookie_jar_http_header(struct cookie_jar *jar, struct strmap *headers,
                       const char *domain, pool_t pool)
{
    static const size_t buffer_size = 4096;
    char *buffer;
    struct cookie *cookie;
    size_t length;
    struct pool_mark mark;

    if (list_empty(&jar->cookies))
        return;

    pool_mark(tpool, &mark);
    buffer = p_malloc(tpool, buffer_size);

    length = 0;

    for (cookie = (struct cookie *)jar->cookies.next;
         &cookie->siblings != &jar->cookies;
         cookie = (struct cookie *)cookie->siblings.next) {
        if (!domain_matches(domain, cookie->domain))
            continue;

        if (buffer_size - length < cookie->name.length + 1 + 1 + cookie->value.length * 2 + 1 + 2)
            break;
        memcpy(buffer + length, cookie->name.data, cookie->name.length);
        length += cookie->name.length;
        buffer[length++] = '=';
        if (http_must_quote_token(&cookie->value))
            length += http_quote_string(buffer + length, &cookie->value);
        else {
            memcpy(buffer + length, cookie->value.data, cookie->value.length);
            length += cookie->value.length;
        }
        buffer[length++] = ';';
        buffer[length++] = ' ';
    }

    strmap_addn(headers, "Cookie2", "$Version=\"1\"");
    strmap_addn(headers, "Cookie", p_strndup(pool, buffer, length));

    pool_rewind(tpool, &mark);
}

void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool)
{
    const char *name, *value, *end;

    assert(cookies != NULL);
    assert(p != NULL);

    while (1) {
        value = strchr(p, '=');
        if (value == NULL)
            break;

        name = p_strndup(pool, p, value - p);

        ++value;

        if (*value == '"') {
            ++value;

            end = strchr(value, '"');
            if (end == NULL)
                break;

            value = p_strndup(pool, value, end - value);

            end = strchr(value, ';');
        } else {
            end = strchr(value, ';');

            if (end == NULL)
                value = p_strdup(pool, value);
            else
                value = p_strndup(pool, value, end - value);
        }

        strmap_addn(cookies, name, value);

        if (end == NULL)
            break;

        p = end + 1;
        while (*p != 0 && char_is_whitespace(*p))
            ++p;
    }
}
