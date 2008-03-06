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

void
cookie_list_set_cookie2(pool_t pool, struct list_head *head, const char *value);

void
cookie_list_http_header(struct strmap *headers, struct list_head *head,
                        pool_t pool);

void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool);

#endif
