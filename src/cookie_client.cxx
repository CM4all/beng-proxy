/*
 * Manage cookies sent by the widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_client.hxx"
#include "cookie_jar.hxx"
#include "cookie_string.hxx"
#include "strref2.h"
#include "strref-pool.h"
#include "strref-dpool.h"
#include "strmap.h"
#include "http_string.hxx"
#include "tpool.h"
#include "dpool.h"
#include "expiry.h"
#include "clock.h"

#include <inline/list.h>

#include <stdlib.h>
#include <string.h>

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
    return match == nullptr || memcmp(path, match, strlen(match)) == 0;
}

static void
cookie_list_delete_match(struct dpool *dpool, struct list_head *head,
                         const char *domain, const char *path,
                         const struct strref *name)
{
    struct cookie *cookie, *next;

    assert(domain != nullptr);

    for (cookie = (struct cookie *)head->next;
         &cookie->siblings != head; cookie = next) {
        next = (struct cookie *)cookie->siblings.next;

        if (domain_matches(domain, cookie->domain) &&
            (cookie->path == nullptr
             ? path == nullptr
             : path_matches(cookie->path, path)) &&
            strref_cmp2(&cookie->name, name) == 0) {
            list_remove(&cookie->siblings);
            cookie_free(dpool, cookie);
        }
    }
}

static struct cookie *
parse_next_cookie(struct dpool *pool, struct strref *input)
{
    struct strref name, value;

    cookie_next_name_value(tpool, input, &name, &value, false);
    if (strref_is_empty(&name))
        return nullptr;

    struct cookie *cookie = (struct cookie *)d_malloc(pool, sizeof(*cookie));
    if (cookie == nullptr)
        /* out of memory */
        return nullptr;

    strref_set_dup_d(pool, &cookie->name, &name);
    if (strref_is_empty(&cookie->name)) {
        /* out of memory */
        d_free(pool, cookie);
        return nullptr;
    }

    strref_set_dup_d(pool, &cookie->value, &value);
    if (strref_is_empty(&cookie->value)) {
        /* out of memory */
        strref_free_d(pool, &cookie->name);
        d_free(pool, cookie);
        return nullptr;
    }

    cookie->domain = nullptr;
    cookie->path = nullptr;
    cookie->expires = 0;

    strref_ltrim(input);
    while (!strref_is_empty(input) && input->data[0] == ';') {
        strref_skip(input, 1);

        http_next_name_value(tpool, input, &name, &value);
        if (strref_lower_cmp_literal(&name, "domain") == 0)
            cookie->domain = strref_dup_d(pool, &value);
        else if (strref_lower_cmp_literal(&name, "path") == 0)
            cookie->path = strref_dup_d(pool, &value);
        else if (strref_lower_cmp_literal(&name, "max-age") == 0) {
            unsigned long seconds;
            char *endptr;

            seconds = strtoul(strref_dup(tpool, &value), &endptr, 10);
            if (*endptr == 0) {
                if (seconds == 0)
                    cookie->expires = (time_t)-1;
                else
                    cookie->expires = expiry_touch(seconds);
            }
        }

        strref_ltrim(input);
    }

    return cookie;
}

static bool
apply_next_cookie(struct cookie_jar *jar, struct strref *input,
                  const char *domain, const char *path)
{
    assert(domain != nullptr);

    struct cookie *cookie = parse_next_cookie(jar->pool, input);
    if (cookie == nullptr)
        return false;

    if (cookie->domain == nullptr) {
        cookie->domain = d_strdup(jar->pool, domain);
        if (cookie->domain == nullptr) {
            /* out of memory */
            cookie_free(jar->pool, cookie);
            return false;
        }
    } else if (!domain_matches(domain, cookie->domain)) {
        /* discard if domain mismatch */
        cookie_free(jar->pool, cookie);
        return false;
    }

    if (path != nullptr && cookie->path != nullptr &&
        !path_matches(path, cookie->path)) {
        /* discard if path mismatch */
        cookie_free(jar->pool, cookie);
        return false;
    }

    /* delete the old cookie */
    cookie_list_delete_match(jar->pool, &jar->cookies, cookie->domain,
                             cookie->path,
                             &cookie->name);

    /* add the new one */

    if (cookie->expires == (time_t)-1)
        /* discard expired cookie */
        cookie_free(jar->pool, cookie);
    else
        cookie_jar_add(jar, cookie);

    return true;
}

void
cookie_jar_set_cookie2(struct cookie_jar *jar, const char *value,
                       const char *domain, const char *path)
{
    struct strref input;
    struct pool_mark_state mark;

    strref_set_c(&input, value);

    pool_mark(tpool, &mark);

    while (1) {
        if (!apply_next_cookie(jar, &input, domain, path))
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

char *
cookie_jar_http_header_value(struct cookie_jar *jar,
                             const char *domain, const char *path,
                             struct pool *pool)
{
    static constexpr size_t buffer_size = 4096;

    assert(domain != nullptr);
    assert(path != nullptr);

    if (list_empty(&jar->cookies))
        return nullptr;

    struct pool_mark_state mark;
    pool_mark(tpool, &mark);

    char *buffer = (char *)p_malloc(tpool, buffer_size);

    size_t length = 0;

    const unsigned now = now_s();

    struct cookie *cookie, *next;
    for (cookie = (struct cookie *)jar->cookies.next;
         &cookie->siblings != &jar->cookies;
         cookie = next) {
        next = (struct cookie *)cookie->siblings.next;
        if (cookie->expires != 0 && (unsigned)cookie->expires < now) {
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

    char *value;
    if (length > 0)
        value = p_strndup(pool, buffer, length);
    else
        value = nullptr;

    pool_rewind(tpool, &mark);
    return value;
}

void
cookie_jar_http_header(struct cookie_jar *jar,
                       const char *domain, const char *path,
                       struct strmap *headers, struct pool *pool)
{
    char *cookie = cookie_jar_http_header_value(jar, domain, path, pool);

    if (cookie != nullptr) {
        strmap_add(headers, "cookie2", "$Version=\"1\"");
        strmap_add(headers, "cookie", cookie);
    }
}
