/*
 * Cookie management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_COOKIE_H
#define __BENG_COOKIE_H

#include "pool.h"
#include "list.h"
#include "growing-buffer.h"

#include <time.h>

struct cookie {
    struct list_head siblings;

    const char *name, *value;
    time_t valid_until;
};

void
cookie_list_set_cookie2(pool_t pool, struct list_head *head, const char *value);

void
cookie_list_http_header(growing_buffer_t gb, struct list_head *head);

#endif
