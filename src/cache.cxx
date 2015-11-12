/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cache.hxx"
#include "hashmap.hxx"
#include "AllocatorStats.hxx"
#include "pool.hxx"
#include "event/CleanupTimer.hxx"
#include "system/clock.h"

#include <assert.h>
#include <time.h>
#include <event.h>

/* #define ENABLE_EXCESSIVE_CACHE_CHECKS */

struct cache {
    struct pool &pool;

    const struct cache_class &cls;
    const size_t max_size;
    size_t size;
    struct hashmap *const items;

    /**
     * A linked list of all cache items, sorted by last_accessed,
     * oldest first.
     */
    boost::intrusive::list<struct cache_item,
                           boost::intrusive::member_hook<struct cache_item,
                                                         cache_item::SiblingsHook,
                                                         &cache_item::sorted_siblings>,
                           boost::intrusive::constant_time_size<false>> sorted_items;

    CleanupTimer cleanup_timer;

    cache(struct pool &_pool, const struct cache_class &_cls,
          unsigned hashtable_capacity, size_t _max_size)
        :pool(_pool), cls(_cls),
         max_size(_max_size), size(0),
         items(hashmap_new(&_pool, hashtable_capacity)) {
        cleanup_timer.Init(60, ExpireCallback, this);
    }

    ~cache();

    /** clean up expired cache items every 60 seconds */
    bool ExpireCallback();
    static bool ExpireCallback(void *ctx);

    void Check() const;

    void ItemRemoved(struct cache_item *item);
};

struct cache *
cache_new(struct pool &pool, const struct cache_class *cls,
          unsigned hashtable_capacity, size_t max_size)
{
    assert(cls != nullptr);

    return new cache(pool, *cls, hashtable_capacity, max_size);
}

inline
cache::~cache()
{
    cleanup_timer.Deinit();

    Check();

    if (cls.destroy != nullptr) {
        hashmap_rewind(items);

        const struct hashmap_pair *pair;
        while ((pair = hashmap_next(items)) != nullptr) {
            struct cache_item *item = (struct cache_item *)pair->value;

            assert(item->lock == 0);
            assert(size >= item->size);
            size -= item->size;

#ifndef NDEBUG
            sorted_items.erase(sorted_items.iterator_to(*item));
#endif

            cls.destroy(item);
        }

        assert(size == 0);
        assert(sorted_items.empty());
    }
}

void
cache_close(struct cache *cache)
{
    delete cache;
}

AllocatorStats
cache_get_stats(const struct cache &cache)
{
    AllocatorStats stats;
    stats.netto_size = pool_children_netto_size(&cache.pool);
    stats.brutto_size = pool_children_brutto_size(&cache.pool);
    return stats;
}

inline void
cache::Check() const
{
#if !defined(NDEBUG) && defined(ENABLE_EXCESSIVE_CACHE_CHECKS)
    const struct hashmap_pair *pair;
    size_t s = 0;

    assert(size <= max_size);

    hashmap_rewind(items);
    while ((pair = hashmap_next(items)) != nullptr) {
        struct cache_item *item = (struct cache_item *)pair->value;

        s += item->size;
        assert(s <= size);
    }

    assert(s == size);
#endif
}

static void
cache_destroy_item(struct cache *cache, struct cache_item *item)
{
    if (cache->cls.destroy != nullptr)
        cache->cls.destroy(item);
}

void
cache::ItemRemoved(struct cache_item *item)
{
    assert(item != nullptr);
    assert(item->size > 0);
    assert(item->lock > 0 || !item->removed);
    assert(size >= item->size);

    sorted_items.erase(sorted_items.iterator_to(*item));

    size -= item->size;

    if (item->lock == 0)
        cache_destroy_item(this, item);
    else
        /* this item is locked - postpone the destroy() call */
        item->removed = true;

    if (size == 0)
        cleanup_timer.Disable();
}

static bool
cache_flush_callback(gcc_unused const char *key, void *value, void *ctx)
{
    struct cache *cache = (struct cache *)ctx;
    struct cache_item *item = (struct cache_item *)value;

    cache->ItemRemoved(item);
    return true;
}

void
cache_flush(struct cache *cache)
{
    cache->Check();

    hashmap_remove_all_match(cache->items, cache_flush_callback, cache);

    cache->Check();
}

static bool
cache_item_validate(const struct cache *cache, struct cache_item *item,
                    unsigned now)
{
    return now < item->expires &&
        (cache->cls.validate == nullptr || cache->cls.validate(item));
}

static void
cache_refresh_item(struct cache *cache, struct cache_item *item, unsigned now)
{
    item->last_accessed = now;

    /* move to the front of the linked list */
    cache->sorted_items.erase(cache->sorted_items.iterator_to(*item));
    cache->sorted_items.push_back(*item);
}

struct cache_item *
cache_get(struct cache *cache, const char *key)
{
    struct cache_item *item = (struct cache_item *)
        hashmap_get(cache->items, key);
    if (item == nullptr)
        return nullptr;

    const unsigned now = now_s();

    if (!cache_item_validate(cache, item, now)) {
        cache->Check();

        hashmap_remove_existing(cache->items, key, item);

        cache->ItemRemoved(item);

        cache->Check();
        return nullptr;
    }

    cache_refresh_item(cache, item, now);
    return item;
}

struct cache_item *
cache_get_match(struct cache *cache, const char *key,
                bool (*match)(const struct cache_item *, void *),
                void *ctx)
{
    const unsigned now = now_s();
    const struct hashmap_pair *pair = nullptr;

    while (true) {
        if (pair != nullptr) {
            struct cache_item *item = (struct cache_item *)pair->value;

            if (!cache_item_validate(cache, item, now)) {
                /* expired cache item: delete it, and re-start the
                   search */

                cache->Check();

                hashmap_remove_existing(cache->items, key, item);

                cache->ItemRemoved(item);
                cache->Check();

                pair = nullptr;
                continue;
            }

            if (match(item, ctx)) {
                /* this one matches: return it to the caller */
                cache_refresh_item(cache, item, now);
                return item;
            }

            /* find the next cache_item for this key */
            pair = hashmap_lookup_next(pair);
        } else {
            /* find the first cache_item for this key */
            pair = hashmap_lookup_first(cache->items, key);
        }

        if (pair == nullptr)
            /* no match */
            return nullptr;
    };
}

static void
cache_destroy_oldest_item(struct cache *cache)
{
    if (cache->sorted_items.empty())
        return;

    struct cache_item &item = cache->sorted_items.front();

    cache->Check();

    hashmap_remove_existing(cache->items, item.key, &item);

    cache->ItemRemoved(&item);
    cache->Check();
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

bool
cache_add(struct cache *cache, const char *key,
          struct cache_item *item)
{
    /* XXX size constraints */
    if (!cache_need_room(cache, item->size)) {
        if (cache->cls.destroy != nullptr)
            cache->cls.destroy(item);
        return false;
    }

    cache->Check();

    item->key = key;
    hashmap_add(cache->items, key, item);
    cache->sorted_items.push_back(*item);

    cache->size += item->size;
    item->last_accessed = now_s();

    cache->Check();

    cache->cleanup_timer.Enable();
    return true;
}

bool
cache_put(struct cache *cache, const char *key,
          struct cache_item *item)
{
    /* XXX size constraints */

    assert(item != nullptr);
    assert(item->size > 0);
    assert(item->lock == 0);
    assert(!item->removed);

    if (!cache_need_room(cache, item->size)) {
        if (cache->cls.destroy != nullptr)
            cache->cls.destroy(item);
        return false;
    }

    cache->Check();

    item->key = key;

    struct cache_item *old = (struct cache_item *)
        hashmap_set(cache->items, key, item);
    if (old != nullptr)
        cache->ItemRemoved(old);

    cache->size += item->size;
    item->last_accessed = now_s();

    cache->sorted_items.push_back(*item);

    cache->Check();

    cache->cleanup_timer.Enable();
    return true;
}

bool
cache_put_match(struct cache *cache, const char *key,
                struct cache_item *item,
                bool (*match)(const struct cache_item *, void *),
                void *ctx)
{
    struct cache_item *old = cache_get_match(cache, key, match, ctx);

    assert(item != nullptr);
    assert(item->size > 0);
    assert(item->lock == 0);
    assert(!item->removed);
    assert(old == nullptr || !old->removed);

    if (old != nullptr)
        cache_remove_item(cache, key, old);

    return cache_add(cache, key, item);
}

void
cache_remove(struct cache *cache, const char *key)
{
    struct cache_item *item;

    while ((item = (struct cache_item *)hashmap_remove(cache->items, key)) != nullptr)
        cache->ItemRemoved(item);

    cache->Check();
}

struct cache_remove_match_data {
    struct cache *cache;
    bool (*match)(const struct cache_item *, void *);
    void *ctx;
};

static bool
cache_remove_match_callback(void *value, void *ctx)
{
    const struct cache_remove_match_data *data =
        (const struct cache_remove_match_data *)ctx;
    struct cache_item *item = (struct cache_item *)value;

    if (data->match(item, data->ctx)) {
        data->cache->ItemRemoved(item);
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

    cache->Check();
    hashmap_remove_match(cache->items, key,
                         cache_remove_match_callback, &data);
    cache->Check();
}

void
cache_remove_item(struct cache *cache, const char *key,
                  struct cache_item *item)
{
    if (item->removed) {
        /* item has already been removed by somebody else */
        assert(item->lock > 0);
        return;
    }

    bool found = hashmap_remove_value(cache->items, key, item);
    if (!found) {
        /* the specified item has been removed before */
        cache->Check();
        return;
    }

    cache->ItemRemoved(item);

    cache->Check();
}

struct cache_remove_all_match_data {
    struct cache *cache;
    bool (*match)(const struct cache_item *, void *);
    void *ctx;
};

static bool
cache_remove_all_match_callback(gcc_unused const char *key, void *value,
                                void *ctx)
{
    const struct cache_remove_all_match_data *data =
        (const struct cache_remove_all_match_data *)ctx;
    struct cache_item *item = (struct cache_item *)value;

    if (data->match(item, data->ctx)) {
        data->cache->ItemRemoved(item);
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

    cache->Check();
    removed = hashmap_remove_all_match(cache->items,
                                       cache_remove_all_match_callback, &data);
    cache->Check();

    return removed;
}

void
cache_item_init_absolute(struct cache_item *item, time_t expires, size_t size)
{
    time_t now = time(nullptr);
    unsigned monotonic_expires = expires > now
        ? now_s() + (expires - now)
        : 1;

    cache_item_init(item, monotonic_expires, size);
}

void
cache_item_init_relative(struct cache_item *item, unsigned max_age,
                         size_t size)
{
    cache_item_init(item, now_s() + max_age, size);
}

void
cache_item_lock(struct cache_item *item)
{
    assert(item != nullptr);

    ++item->lock;
}

void
cache_item_unlock(struct cache *cache, struct cache_item *item)
{
    assert(item != nullptr);
    assert(item->lock > 0);

    if (--item->lock == 0 && item->removed)
        /* postponed destroy */
        cache_destroy_item(cache, item);
}

/** clean up expired cache items every 60 seconds */
bool
cache::ExpireCallback()
{
    const unsigned now = now_s();

    Check();

    for (auto i = sorted_items.begin(), end = sorted_items.end(); i != end;) {
        struct cache_item &item = *i++;

        if (item.expires > now)
            /* not yet expired */
            continue;

        hashmap_remove_existing(items, item.key, &item);
        ItemRemoved(&item);
    }

    Check();

    return size > 0;
}

bool
cache::ExpireCallback(void *ctx)
{
    struct cache *cache = (struct cache *)ctx;

    return cache->ExpireCallback();
}

void
cache_event_add(struct cache *cache)
{
    if (cache->size > 0)
        cache->cleanup_timer.Enable();
}

void
cache_event_del(struct cache *cache)
{
    cache->cleanup_timer.Disable();
}
