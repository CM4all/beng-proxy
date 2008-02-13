/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CACHE_H
#define __BENG_CACHE_H

#include "pool.h"

#include <sys/time.h>

struct cache;

struct cache_item {
    time_t expires;
};

struct cache_class {
    int (*validate)(struct cache_item *item);
    void (*destroy)(struct cache_item *item);
};

struct cache *
cache_new(pool_t pool, const struct cache_class *class);

void
cache_close(struct cache *cache);

struct cache_item *
cache_get(struct cache *cache, const char *key);

void
cache_put(struct cache *cache, const char *key,
          struct cache_item *item);

#endif
