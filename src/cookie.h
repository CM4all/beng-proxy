/*
 * Cookie management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_COOKIE_H
#define __BENG_COOKIE_H

#include "pool.h"
#include "strref.h"

#include <inline/list.h>

#include <time.h>

struct strmap;

struct cookie;

struct cookie_jar {
    pool_t pool;

    struct list_head cookies;
};

static inline void
cookie_jar_init(struct cookie_jar *jar, pool_t pool)
{
    jar->pool = pool;
    list_init(&jar->cookies);
}

/**
 * Parse a Set-Cookie2 response header and insert new cookies into the
 * linked list.
 */
void
cookie_jar_set_cookie2(struct cookie_jar *jar, const char *value,
                       const char *domain);

/**
 * Generate HTTP request headers passing for all cookies in the linked
 * list.
 */
void
cookie_jar_http_header(struct cookie_jar *jar, struct strmap *headers,
                       const char *domain, pool_t pool);

/**
 * Parse a Cookie request header and store all cookies in the
 * specified strmap.
 */
void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool);

#endif
