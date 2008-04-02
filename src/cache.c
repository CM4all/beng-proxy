/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cache.h"
#include "hashmap.h"

#include <assert.h>
#include <time.h>

struct cache {
    const struct cache_class *class;
    size_t max_size, size;
    struct hashmap *items;
};

struct cache *
cache_new(pool_t pool, const struct cache_class *class,
          size_t max_size)
{
    struct cache *cache = p_malloc(pool, sizeof(*cache));

    assert(class != NULL);

    cache->class = class;
    cache->max_size = max_size;
    cache->size = 0;
    cache->items = hashmap_new(pool, 1024);

    /* event, auto-expire */

    return cache;
}

void
cache_close(struct cache *cache)
{
    const struct hashmap_pair *pair;

    if (cache->class->destroy != NULL) {
        hashmap_rewind(cache->items);
        while ((pair = hashmap_next(cache->items)) != NULL) {
            struct cache_item *item = pair->value;

            assert(cache->size >= item->size);
            cache->size -= item->size;

            cache->class->destroy(item);
        }

        assert(cache->size == 0);
    }
}

static void
cache_destroy_item(struct cache *cache, struct cache_item *item)
{
    assert(cache->size >= item->size);
    cache->size -= item->size;

    if (cache->class->destroy != NULL)
        cache->class->destroy(item);
}

struct cache_item *
cache_get(struct cache *cache, const char *key)
{
    struct cache_item *item = hashmap_get(cache->items, key);
    if (item == NULL)
        return NULL;

    if (time(NULL) < item->expires &&
        (cache->class->validate == NULL || cache->class->validate(item))) {
        item->last_accessed = time(NULL);
        return item;
    }

    hashmap_remove(cache->items, key);
    cache_destroy_item(cache, item);
    return NULL;
}

static void
cache_destroy_oldest_item(struct cache *cache)
{
    const struct hashmap_pair *pair;
    const char *oldest_key;
    struct cache_item *oldest_item = NULL;

    /* XXX this function is O(n^2) */

    hashmap_rewind(cache->items);
    while ((pair = hashmap_next(cache->items)) != NULL) {
        struct cache_item *item = pair->value;

        if (oldest_item == NULL ||
            item->last_accessed < oldest_item->last_accessed) {
            oldest_key = pair->key;
            oldest_item = item;
        }
    }

    if (oldest_item == NULL)
        return;

    hashmap_remove(cache->items, oldest_key);
    cache_destroy_item(cache, oldest_item);
}

static int
cache_need_room(struct cache *cache, size_t size)
{
    if (size > cache->max_size)
        return 0;

    while (1) {
        if (cache->size + size <= cache->max_size)
            return 1;

        cache_destroy_oldest_item(cache);
    }
}

void
cache_put(struct cache *cache, const char *key,
          struct cache_item *item)
{
    /* XXX size constraints */
    struct cache_item *old;

    if (!cache_need_room(cache, item->size)) {
        if (cache->class->destroy != NULL)
            cache->class->destroy(item);
        return;
    }

    old = hashmap_put(cache->items, key, item, 1);
    if (old != NULL)
        cache_destroy_item(cache, old);

    cache->size += item->size;
    item->last_accessed = time(NULL);
}

void
cache_remove(struct cache *cache, const char *key, struct cache_item *item)
{
    struct cache_item *old = hashmap_remove(cache->items, key);

    if (old != item) {
        /* the specified item has been removed before */
        if (old != NULL)
            hashmap_put(cache->items, key, old, 1);
        return;
    }

    cache_destroy_item(cache, item);
}
