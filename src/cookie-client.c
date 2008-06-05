/*
 * Manage cookies sent by the widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie-client.h"
#include "strutil.h"
#include "strref2.h"
#include "strref-pool.h"
#include "strref-dpool.h"
#include "strmap.h"
#include "http-string.h"
#include "tpool.h"
#include "dpool.h"

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
    struct dpool *pool;

    struct list_head cookies;
};

static void
cookie_free(struct dpool *pool, struct cookie *cookie)
{
    if (!strref_is_empty(&cookie->name))
        strref_free_d(pool, &cookie->name);

    if (!strref_is_empty(&cookie->value))
        strref_free_d(pool, &cookie->value);

    if (cookie->domain != NULL)
        d_free(pool, cookie->domain);

    if (cookie->path != NULL)
        d_free(pool, cookie->path);

    d_free(pool, cookie);
}

static void
cookie_delete(struct cookie_jar *jar, struct cookie *cookie)
{
    assert(jar != NULL);
    assert(cookie != NULL);
    assert(&cookie->siblings != &jar->cookies);

    list_remove(&cookie->siblings);

    cookie_free(jar->pool, cookie);
}

struct cookie_jar *
cookie_jar_new(struct dpool *pool)
{
    struct cookie_jar *jar = d_malloc(pool, sizeof(*jar));
    if (jar == NULL)
        return NULL;

    jar->pool = pool;
    list_init(&jar->cookies);
    return jar;
}

void
cookie_jar_free(struct cookie_jar *jar)
{
    while (!list_empty(&jar->cookies)) {
        struct cookie *cookie = (struct cookie *)jar->cookies.next;

        list_remove(&cookie->siblings);
        cookie_free(jar->pool, cookie);
    }

    d_free(jar->pool, jar);
}

static bool
domain_matches(const char *domain, const char *match)
{
    size_t domain_length = strlen(domain);
    size_t match_length = strlen(match);

    return domain_length >= match_length &&
        strcasecmp(domain + domain_length - match_length, match) == 0 &&
        (domain_length == match_length || /* "a.b" matches "a.b" */
         match[0] == '.' || /* "a.b" matches ".b" */
         /* "a.b" matches "b" (implicit dot according to RFC 2965
            3.2.2): */
         (domain_length > match_length &&
          domain[domain_length - match_length - 1] == '.'));
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

    http_next_name_value(tpool, input, &name, &value);
    if (strref_is_empty(&name))
        return false;

    cookie = cookie_list_find(&jar->cookies, domain, name.data, name.length);
    if (cookie == NULL) {
        cookie = d_malloc(jar->pool, sizeof(*cookie));
        if (cookie == NULL)
            /* out of memory */
            return false;

        strref_set_dup_d(jar->pool, &cookie->name, &name);
        if (strref_is_empty(&cookie->name)) {
            /* out of memory */
            d_free(jar->pool, cookie);
            return false;
        }

        list_add(&cookie->siblings, &jar->cookies);
    }

    cookie->domain = NULL;
    cookie->path = NULL;
    cookie->expires = 0;

    strref_set_dup_d(jar->pool, &cookie->value, &value);
    /* XXX check out of memory */

    strref_ltrim(input);
    while (!strref_is_empty(input) && input->data[0] == ';') {
        strref_skip(input, 1);

        http_next_name_value(tpool, input, &name, &value);
        if (strref_lower_cmp_literal(&name, "domain") == 0) {
            const char *new_domain = strref_dup(tpool, &value);
            if (strcasecmp(new_domain, "local") != 0 &&
                strcasecmp(new_domain, ".local") != 0 &&
                domain_matches(domain, new_domain))
                cookie->domain = d_strdup(jar->pool, new_domain);
        } else if (strref_lower_cmp_literal(&name, "path") == 0)
            cookie->path = strref_dup_d(jar->pool, &value);
        else if (strref_lower_cmp_literal(&name, "max-age") == 0) {
            unsigned long seconds;
            char *endptr;

            seconds = strtoul(strref_dup(tpool, &value), &endptr, 10);
            if (seconds > 0 && *endptr == 0)
                cookie->expires = time(NULL) + seconds;
        }

        strref_ltrim(input);
    }

    if (cookie->domain == NULL)
        cookie->domain = d_strdup(jar->pool, domain);

    return true;
}

void
cookie_jar_set_cookie2(struct cookie_jar *jar, const char *value,
                       const char *domain)
{
    struct strref input;
    struct pool_mark mark;

    strref_set_c(&input, value);

    pool_mark(tpool, &mark);

    while (1) {
        if (!parse_next_cookie(jar, &input, domain))
            break;

        if (strref_is_empty(&input)) {
            pool_rewind(tpool, &mark);
            return;
        }

        if (input.data[0] != ',')
            break;

        strref_skip(&input, 1);
        strref_ltrim(&input);
    }

    pool_rewind(tpool, &mark);

    /* XXX log error */
}

void
cookie_jar_http_header(struct cookie_jar *jar,
                       const char *domain, const char *path,
                       struct strmap *headers, pool_t pool)
{
    static const size_t buffer_size = 4096;
    char *buffer;
    struct cookie *cookie, *next;
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
         cookie = next) {
        next = (struct cookie *)cookie->siblings.next;
        if (cookie->expires != 0 && cookie->expires < now) {
            cookie_delete(jar, cookie);
            continue;
        }

        if (!domain_matches(domain, cookie->domain) ||
            !path_matches(path, cookie->path))
            continue;

        if (buffer_size - length < cookie->name.length + 1 + 1 + cookie->value.length * 2 + 1 + 2)
            break;

        if (length > 0) {
            buffer[length++] = ';';
            buffer[length++] = ' ';
        }

        memcpy(buffer + length, cookie->name.data, cookie->name.length);
        length += cookie->name.length;
        buffer[length++] = '=';
        if (http_must_quote_token(&cookie->value))
            length += http_quote_string(buffer + length, &cookie->value);
        else {
            memcpy(buffer + length, cookie->value.data, cookie->value.length);
            length += cookie->value.length;
        }
    }

    if (length > 0) {
        strmap_add(headers, "cookie2", "$Version=\"1\"");
        strmap_add(headers, "cookie", p_strndup(pool, buffer, length));
    }
   
    pool_rewind(tpool, &mark);
}
