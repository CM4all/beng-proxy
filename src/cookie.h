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

struct cookie {
    struct list_head siblings;

    struct strref name;
    struct strref value;
    time_t valid_until;
};

/**
 * Parse a Set-Cookie2 response header and insert new cookies into the
 * linked list.
 */
void
cookie_list_set_cookie2(pool_t pool, struct list_head *head, const char *value);

/**
 * Generate HTTP request headers passing for all cookies in the linked
 * list.
 */
void
cookie_list_http_header(struct strmap *headers, struct list_head *head,
                        pool_t pool);

/**
 * Parse a Cookie request header and store all cookies in the
 * specified strmap.
 */
void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool);

#endif
