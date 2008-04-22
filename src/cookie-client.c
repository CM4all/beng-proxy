/*
 * Manage cookies sent by the widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie-client.h"
#include "strutil.h"
#include "strref2.h"
#include "strmap.h"
#include "http-string.h"
#include "tpool.h"

#include <inline/list.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

struct cookie {
    struct list_head siblings;

    struct strref name;
    struct strref value;
    const char *domain, *path;
    time_t expires;
};

struct cookie_jar {
    pool_t pool;

    struct list_head cookies;
};

struct cookie_jar *
cookie_jar_new(pool_t pool)
{
    struct cookie_jar *jar = p_malloc(pool, sizeof(*jar));
    jar->pool = pool;
    list_init(&jar->cookies);
    return jar;
}

static bool
domain_matches(const char *domain, const char *match)
{
    /* XXX */

    return strcmp(domain, match) == 0;
}

static bool
path_matches(const char *path, const char *match)
{
    return match == NULL || memcmp(path, match, strlen(match)) == 0;
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

static bool
parse_next_cookie(struct cookie_jar *jar, struct strref *input,
                  const char *domain)
{
    struct strref name, value;
    struct cookie *cookie;

    http_next_name_value(jar->pool, input, &name, &value);
    if (strref_is_empty(&name))
        return false;

    cookie = cookie_list_find(&jar->cookies, domain, name.data, name.length);
    if (cookie == NULL) {
        cookie = p_malloc(jar->pool, sizeof(*cookie));

        strref_set_dup(jar->pool, &cookie->name, &name);

        list_add(&cookie->siblings, &jar->cookies);
    }

    cookie->domain = NULL;
    cookie->path = NULL;
    cookie->expires = 0;

    strref_set_dup(jar->pool, &cookie->value, &value);

    strref_ltrim(input);
    while (!strref_is_empty(input) && input->data[0] == ';') {
        strref_skip(input, 1);

        http_next_name_value(jar->pool, input, &name, &value);
        if (strref_lower_cmp_literal(&name, "domain") == 0) {
            const char *new_domain = strref_dup(jar->pool, &value);
            if (domain_matches(domain, new_domain))
                cookie->domain = new_domain;
        } else if (strref_lower_cmp_literal(&name, "path") == 0)
            cookie->path = strref_dup(jar->pool, &value);
        else if (strref_lower_cmp_literal(&name, "max-age") == 0) {
            unsigned long seconds;
            char *endptr;

            seconds = strtoul(strref_dup(jar->pool, &value), &endptr, 10);
            if (seconds > 0 && *endptr == 0)
                cookie->expires = time(NULL) + seconds;
        }

        strref_ltrim(input);
    }

    if (cookie->domain == NULL)
        cookie->domain = p_strdup(jar->pool, domain);

    return true;
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
        strref_ltrim(&input);
    }

    /* XXX log error */
}

void
cookie_jar_http_header(struct cookie_jar *jar,
                       const char *domain, const char *path,
                       struct strmap *headers, pool_t pool)
{
    static const size_t buffer_size = 4096;
    char *buffer;
    struct cookie *cookie;
    size_t length;
    struct pool_mark mark;
    time_t now;

    assert(domain != NULL);
    assert(path != NULL);

    if (list_empty(&jar->cookies))
        return;

    pool_mark(tpool, &mark);
    buffer = p_malloc(tpool, buffer_size);

    length = 0;
    now = time(NULL);

    for (cookie = (struct cookie *)jar->cookies.next;
         &cookie->siblings != &jar->cookies;
         cookie = (struct cookie *)cookie->siblings.next) {
        if (!domain_matches(domain, cookie->domain) ||
            !path_matches(path, cookie->path) ||
            (cookie->expires != 0 && cookie->expires < now))
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
