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
    size_t size;
    time_t last_accessed;
};

struct cache_class {
    bool (*validate)(struct cache_item *item);
    void (*destroy)(struct cache_item *item);
};

struct cache *
cache_new(pool_t pool, const struct cache_class *class,
          size_t max_size);

void
cache_close(struct cache *cache);

struct cache_item *
cache_get(struct cache *cache, const char *key);

/**
 * Find the first cache_item for a key which matches with the
 * specified matching function.
 *
 * @param cache the cache object
 * @param key the cache item key
 * @param match the match callback function
 * @param ctx a context pointer for the callback
 */
struct cache_item *
cache_get_match(struct cache *cache, const char *key,
                bool (*match)(const struct cache_item *, void *),
                void *ctx);

/**
 * Add an item to this cache.  Item with the same key are preserved.
 */
void
cache_add(struct cache *cache, const char *key,
          struct cache_item *item);

void
cache_put(struct cache *cache, const char *key,
          struct cache_item *item);

/**
 * Adds a new item to this cache, or replaces an existing item which
 * matches with the specified matching function.
 *
 * @param cache the cache object
 * @param key the cache item key
 * @param item the new cache item
 * @param match the match callback function
 * @param ctx a context pointer for the callback
 */
void
cache_put_match(struct cache *cache, const char *key,
                struct cache_item *item,
                bool (*match)(const struct cache_item *, void *),
                void *ctx);

void
cache_remove(struct cache *cache, const char *key);

void
cache_remove_item(struct cache *cache, const char *key,
                  struct cache_item *item);

#endif
