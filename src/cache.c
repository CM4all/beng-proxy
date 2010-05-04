/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cache.h"
#include "hashmap.h"

#include <assert.h>
#include <time.h>
#include <event.h>

struct cache {
    const struct cache_class *class;
    size_t max_size, size;
    struct hashmap *items;

    /**
     * A linked list of all cache items, sorted by last_accessed,
     * newest first.
     */
    struct list_head sorted_items;

    struct event expire_event;
};

/** clean up expired cache items every 60 seconds */
static const struct timeval cache_expire_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static void
cache_expire_event_callback(int fd __attr_unused, short event __attr_unused,
                            void *ctx);

struct cache *
cache_new(pool_t pool, const struct cache_class *class,
          unsigned hashtable_capacity, size_t max_size)
{
    struct cache *cache = p_malloc(pool, sizeof(*cache));
    struct timeval tv = cache_expire_interval;

    assert(class != NULL);

    cache->class = class;
    cache->max_size = max_size;
    cache->size = 0;
    cache->items = hashmap_new(pool, hashtable_capacity);
    list_init(&cache->sorted_items);

    evtimer_set(&cache->expire_event, cache_expire_event_callback, cache);
    evtimer_add(&cache->expire_event, &tv);

    return cache;
}

static void
cache_check(const struct cache *cache);

void
cache_close(struct cache *cache)
{
    const struct hashmap_pair *pair;

    evtimer_del(&cache->expire_event);

    cache_check(cache);

    if (cache->class->destroy != NULL) {
        hashmap_rewind(cache->items);
        while ((pair = hashmap_next(cache->items)) != NULL) {
            struct cache_item *item = pair->value;

            assert(item->lock == 0);
            assert(cache->size >= item->size);
            cache->size -= item->size;

#ifndef NDEBUG
            list_remove(&item->sorted_siblings);
#endif

            cache->class->destroy(item);
        }

        assert(cache->size == 0);
        assert(list_empty(&cache->sorted_items));
    }
}

static inline struct cache_item *
list_head_to_cache_item(struct list_head *list_head)
{
    return (struct cache_item *)(((char*)list_head) - offsetof(struct cache_item, sorted_siblings));
}

static void
cache_check(const struct cache *cache)
{
#ifndef NDEBUG
    const struct hashmap_pair *pair;
    size_t size = 0;

    assert(cache != NULL);
    assert(cache->size <= cache->max_size);

    hashmap_rewind(cache->items);
    while ((pair = hashmap_next(cache->items)) != NULL) {
        struct cache_item *item = pair->value;

        size += item->size;
        assert(size <= cache->size);
    }

    assert(size == cache->size);
#else
    (void)cache;
#endif
}

static void
cache_destroy_item(struct cache *cache, struct cache_item *item)
{
    if (cache->class->destroy != NULL)
        cache->class->destroy(item);
}

static void
cache_item_removed(struct cache *cache, struct cache_item *item)
{
    assert(cache != NULL);
    assert(item != NULL);
    assert(item->size > 0);
    assert(item->lock > 0 || !item->removed);
    assert(cache->size >= item->size);

    list_remove(&item->sorted_siblings);

    cache->size -= item->size;

    if (item->lock == 0)
        cache_destroy_item(cache, item);
    else
        /* this item is locked - postpone the destroy() call */
        item->removed = true;
}

void
cache_flush(struct cache *cache)
{
    struct cache_item *item;

    cache_check(cache);

    for (item = (struct cache_item *)cache->sorted_items.next;
         &item->sorted_siblings != &cache->sorted_items;
         item = (struct cache_item *)item->sorted_siblings.next) {
        bool found;
        struct cache_item *item2;

        found = hashmap_remove_value(cache->items, item->key, item);
        assert(found);

        item2 = item;
        item = (struct cache_item *)item->sorted_siblings.prev;
        cache_item_removed(cache, item2);
    }

    cache_check(cache);
}

static bool
cache_item_validate(const struct cache *cache, struct cache_item *item)
{
    return time(NULL) < item->expires &&
        (cache->class->validate == NULL || cache->class->validate(item));
}

static void
cache_refresh_item(struct cache *cache, struct cache_item *item)
{
    item->last_accessed = time(NULL);

    /* move to the front of the linked list */
    list_remove(&item->sorted_siblings);
    list_add(&item->sorted_siblings, &cache->sorted_items);
}

struct cache_item *
cache_get(struct cache *cache, const char *key)
{
    struct cache_item *item = hashmap_get(cache->items, key);
    if (item == NULL)
        return NULL;

    if (!cache_item_validate(cache, item)) {
        bool found;

        cache_check(cache);
        found = hashmap_remove_value(cache->items, key, item);
        assert(found);

        cache_item_removed(cache, item);

        cache_check(cache);
        return NULL;
    }

    cache_refresh_item(cache, item);
    return item;
}

struct cache_item *
cache_get_match(struct cache *cache, const char *key,
                bool (*match)(const struct cache_item *, void *),
                void *ctx)
{
    struct cache_item *item = NULL;

    while (true) {
        if (item != NULL) {
            if (!cache_item_validate(cache, item)) {
                /* expired cache item: delete it, and re-start the
                   search */
                bool found;

                cache_check(cache);
                found = hashmap_remove_value(cache->items, key, item);
                assert(found);

                cache_item_removed(cache, item);
                cache_check(cache);

                item = NULL;
                continue;
            }

            if (match(item, ctx)) {
                /* this one matches: return it to the caller */
                cache_refresh_item(cache, item);
                return item;
            }

            /* find the next cache_item for this key */
            item = hashmap_get_next(cache->items, key, item);
        } else {
            /* find the first cache_item for this key */
            item = hashmap_get(cache->items, key);
        }

        if (item == NULL)
            /* no match */
            return NULL;
    };
}

static void
cache_destroy_oldest_item(struct cache *cache)
{
    struct cache_item *item;
    bool found;

    if (list_empty(&cache->sorted_items))
        return;

    item = list_head_to_cache_item(cache->sorted_items.prev);

    cache_check(cache);
    found = hashmap_remove_value(cache->items, item->key, item);
    assert(found);

    cache_item_removed(cache, item);
    cache_check(cache);
}

static bool
cache_need_room(struct cache *cache, size_t size)
{
    if (size > cache->max_size)
        return false;

    while (1) {
        if (cache->size + size <= cache->max_size)
            return true;

        cache_destroy_oldest_item(cache);
    }
}

void
cache_add(struct cache *cache, const char *key,
          struct cache_item *item)
{
    /* XXX size constraints */
    if (!cache_need_room(cache, item->size)) {
        if (cache->class->destroy != NULL)
            cache->class->destroy(item);
        return;
    }

    cache_check(cache);

    item->key = key;
    hashmap_add(cache->items, key, item);
    list_add(&item->sorted_siblings, &cache->sorted_items);

    cache->size += item->size;
    item->last_accessed = time(NULL);

    cache_check(cache);
}

void
cache_put(struct cache *cache, const char *key,
          struct cache_item *item)
{
    /* XXX size constraints */
    struct cache_item *old;

    assert(item != NULL);
    assert(item->size > 0);
    assert(item->lock == 0);
    assert(!item->removed);

    if (!cache_need_room(cache, item->size)) {
        if (cache->class->destroy != NULL)
            cache->class->destroy(item);
        return;
    }

    cache_check(cache);

    item->key = key;

    old = hashmap_set(cache->items, key, item);
    if (old != NULL)
        cache_item_removed(cache, old);

    cache->size += item->size;
    item->last_accessed = time(NULL);

    list_add(&item->sorted_siblings, &cache->sorted_items);

    cache_check(cache);
}

void
cache_put_match(struct cache *cache, const char *key,
                struct cache_item *item,
                bool (*match)(const struct cache_item *, void *),
                void *ctx)
{
    struct cache_item *old = cache_get_match(cache, key, match, ctx);

    assert(item != NULL);
    assert(item->size > 0);
    assert(item->lock == 0);
    assert(!item->removed);
    assert(old == NULL || !old->removed);

    if (old != NULL)
        cache_remove_item(cache, key, old);

    cache_add(cache, key, item);
}

void
cache_remove(struct cache *cache, const char *key)
{
    struct cache_item *item;

    while ((item = hashmap_remove(cache->items, key)) != NULL)
        cache_item_removed(cache, item);

    cache_check(cache);
}

struct cache_remove_match_data {
    struct cache *cache;
    bool (*match)(const struct cache_item *, void *);
    void *ctx;
};

static bool
cache_remove_match_callback(void *value, void *ctx)
{
    const struct cache_remove_match_data *data = ctx;
    struct cache_item *item = value;

    if (data->match(item, data->ctx)) {
        cache_item_removed(data->cache, item);
        return true;
    } else
        return false;
}

void
cache_remove_match(struct cache *cache, const char *key,
                   bool (*match)(const struct cache_item *, void *),
                   void *ctx)
{
    struct cache_remove_match_data data = {
        .cache = cache,
        .match = match,
        .ctx = ctx,
    };

    cache_check(cache);
    hashmap_remove_match(cache->items, key,
                         cache_remove_match_callback, &data);
    cache_check(cache);
}

void
cache_remove_item(struct cache *cache, const char *key,
                  struct cache_item *item)
{
    bool found;

    if (item->removed) {
        /* item has already been removed by somebody else */
        assert(item->lock > 0);
        return;
    }

    found = hashmap_remove_value(cache->items, key, item);
    if (!found) {
        /* the specified item has been removed before */
        cache_check(cache);
        return;
    }

    cache_item_removed(cache, item);

    cache_check(cache);
}

struct cache_remove_all_match_data {
    struct cache *cache;
    bool (*match)(const struct cache_item *, void *);
    void *ctx;
};

static bool
cache_remove_all_match_callback(__attr_unused const char *key, void *value,
                                void *ctx)
{
    const struct cache_remove_all_match_data *data = ctx;
    struct cache_item *item = value;

    if (data->match(item, data->ctx)) {
        cache_item_removed(data->cache, item);
        return true;
    } else
        return false;
}

unsigned
cache_remove_all_match(struct cache *cache,
                       bool (*match)(const struct cache_item *, void *),
                       void *ctx)
{
    struct cache_remove_all_match_data data = {
        .cache = cache,
        .match = match,
        .ctx = ctx,
    };
    unsigned removed;

    cache_check(cache);
    removed = hashmap_remove_all_match(cache->items,
                                       cache_remove_all_match_callback, &data);
    cache_check(cache);

    return removed;
}

void
cache_item_lock(struct cache_item *item)
{
    assert(item != NULL);

    ++item->lock;
}

void
cache_item_unlock(struct cache *cache, struct cache_item *item)
{
    assert(item != NULL);
    assert(item->lock > 0);

    if (--item->lock == 0 && item->removed)
        /* postponed destroy */
        cache_destroy_item(cache, item);
}

/** clean up expired cache items every 60 seconds */
static void
cache_expire_event_callback(int fd __attr_unused, short event __attr_unused,
                            void *ctx)
{
    struct cache *cache = ctx;
    struct cache_item *item;
    const time_t now = time(NULL);
    struct timeval tv = cache_expire_interval;

    cache_check(cache);

    for (item = (struct cache_item *)cache->sorted_items.next;
         &item->sorted_siblings != &cache->sorted_items;
         item = (struct cache_item *)item->sorted_siblings.next) {
        bool found;
        struct cache_item *item2;

        if (item->expires > now)
            /* not yet expired */
            continue;

        found = hashmap_remove_value(cache->items, item->key, item);
        assert(found);

        item2 = item;
        item = (struct cache_item *)item->sorted_siblings.prev;
        cache_item_removed(cache, item2);
    }

    cache_check(cache);

    evtimer_add(&cache->expire_event, &tv);
}
