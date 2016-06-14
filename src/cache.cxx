/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cache.hxx"
#include "AllocatorStats.hxx"
#include "pool.hxx"
#include "event/CleanupTimer.hxx"
#include "util/djbhash.h"

#include <boost/version.hpp>

#include <assert.h>

/* #define ENABLE_EXCESSIVE_CACHE_CHECKS */

struct Cache {
    struct pool &pool;

    const size_t max_size;
    size_t size;

    typedef boost::intrusive::unordered_multiset<CacheItem,
                                                 boost::intrusive::member_hook<CacheItem,
                                                                               CacheItem::SetHook,
                                                                               &CacheItem::set_hook>,
                                                 boost::intrusive::hash<CacheItem::Hash>,
                                                 boost::intrusive::equal<CacheItem::Equal>,
                                                 boost::intrusive::constant_time_size<false>> ItemSet;

    ItemSet items;

    /**
     * A linked list of all cache items, sorted by last_accessed,
     * oldest first.
     */
    boost::intrusive::list<CacheItem,
                           boost::intrusive::member_hook<CacheItem,
                                                         CacheItem::SiblingsHook,
                                                         &CacheItem::sorted_siblings>,
                           boost::intrusive::constant_time_size<false>> sorted_items;

    CleanupTimer cleanup_timer;

    Cache(struct pool &_pool, EventLoop &event_loop,
          unsigned hashtable_capacity, size_t _max_size)
        :pool(_pool),
         max_size(_max_size), size(0),
         items(ItemSet::bucket_traits(PoolAlloc<ItemSet::bucket_type>(_pool,
                                                                      hashtable_capacity),
                                      hashtable_capacity)),
         cleanup_timer(event_loop, 60, BIND_THIS_METHOD(ExpireCallback)) {}

    ~Cache();

    /** clean up expired cache items every 60 seconds */
    bool ExpireCallback();

    void Check() const;

    void ItemRemoved(CacheItem *item);

    class ItemRemover {
        Cache &cache;

    public:
        explicit ItemRemover(Cache &_cache):cache(_cache) {}

        void operator()(CacheItem *item) {
            cache.ItemRemoved(item);
        }
    };

    void RemoveItem(CacheItem &item) {
        assert(!item.removed);

#if BOOST_VERSION >= 105000
        items.erase_and_dispose(items.iterator_to(item),
                                ItemRemover(*this));
#else
        items.erase(items.iterator_to(item));
        ItemRemoved(&item);
#endif
    }
};

inline size_t
CacheItem::KeyHasher(const char *key)
{
    assert(key != nullptr);

    return djb_hash_string(key);
}

Cache *
cache_new(struct pool &pool, EventLoop &event_loop,
          unsigned hashtable_capacity, size_t max_size)
{
    return new Cache(pool, event_loop, hashtable_capacity, max_size);
}

inline
Cache::~Cache()
{
    Check();

    items.clear_and_dispose([this](CacheItem *item){
            assert(item->lock == 0);
            assert(size >= item->size);
            size -= item->size;

#ifndef NDEBUG
            sorted_items.erase(sorted_items.iterator_to(*item));
#endif

            item->Destroy();
        });

    assert(size == 0);
    assert(sorted_items.empty());
}

void
cache_close(Cache *cache)
{
    delete cache;
}

AllocatorStats
cache_get_stats(const Cache &cache)
{
    AllocatorStats stats;
    stats.netto_size = pool_children_netto_size(&cache.pool);
    stats.brutto_size = pool_children_brutto_size(&cache.pool);
    return stats;
}

inline void
Cache::Check() const
{
#if !defined(NDEBUG) && defined(ENABLE_EXCESSIVE_CACHE_CHECKS)
    const struct hashmap_pair *pair;
    size_t s = 0;

    assert(size <= max_size);

    hashmap_rewind(items);
    while ((pair = hashmap_next(items)) != nullptr) {
        CacheItem *item = (CacheItem *)pair->value;

        s += item->size;
        assert(s <= size);
    }

    assert(s == size);
#endif
}

void
Cache::ItemRemoved(CacheItem *item)
{
    assert(item != nullptr);
    assert(item->size > 0);
    assert(item->lock > 0 || !item->removed);
    assert(size >= item->size);

    sorted_items.erase(sorted_items.iterator_to(*item));

    size -= item->size;

    if (item->lock == 0)
        item->Destroy();
    else
        /* this item is locked - postpone the destroy() call */
        item->removed = true;

    if (size == 0)
        cleanup_timer.Disable();
}

void
cache_flush(Cache *cache)
{
    cache->Check();
    cache->items.clear_and_dispose(Cache::ItemRemover(*cache));
    cache->Check();
}

static bool
cache_item_validate(CacheItem *item,
                    std::chrono::steady_clock::time_point now)
{
    return now < item->expires && item->Validate();
}

static void
cache_refresh_item(Cache *cache, CacheItem *item,
                   std::chrono::steady_clock::time_point now)
{
    item->last_accessed = now;

    /* move to the front of the linked list */
    cache->sorted_items.erase(cache->sorted_items.iterator_to(*item));
    cache->sorted_items.push_back(*item);
}

CacheItem *
cache_get(Cache *cache, const char *key)
{
    auto i = cache->items.find(key, CacheItem::KeyHasher,
                               CacheItem::KeyValueEqual);
    if (i == cache->items.end())
        return nullptr;

    CacheItem *item = &*i;

    const auto now = std::chrono::steady_clock::now();

    if (!cache_item_validate(item, now)) {
        cache->Check();
        cache->RemoveItem(*item);
        cache->Check();
        return nullptr;
    }

    cache_refresh_item(cache, item, now);
    return item;
}

CacheItem *
cache_get_match(Cache *cache, const char *key,
                bool (*match)(const CacheItem *, void *),
                void *ctx)
{
    const auto now = std::chrono::steady_clock::now();

    const auto r = cache->items.equal_range(key, CacheItem::KeyHasher,
                                            CacheItem::KeyValueEqual);
    for (auto i = r.first, end = r.second; i != end;) {
        CacheItem *item = &*i++;

        if (!cache_item_validate(item, now)) {
            /* expired cache item: delete it, and re-start the
               search */

            cache->Check();
            cache->RemoveItem(*item);
            cache->Check();
        } else if (match(item, ctx)) {
            /* this one matches: return it to the caller */
            cache_refresh_item(cache, item, now);
            return item;
        }
    };

    return nullptr;
}

static void
cache_destroy_oldest_item(Cache *cache)
{
    if (cache->sorted_items.empty())
        return;

    CacheItem &item = cache->sorted_items.front();

    cache->Check();
    cache->RemoveItem(item);
    cache->Check();
}

static bool
cache_need_room(Cache *cache, size_t size)
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
cache_add(Cache *cache, const char *key,
          CacheItem *item)
{
    /* XXX size constraints */
    if (!cache_need_room(cache, item->size)) {
        item->Destroy();
        return false;
    }

    cache->Check();

    item->key = key;
    cache->items.insert(*item);
    cache->sorted_items.push_back(*item);

    cache->size += item->size;
    item->last_accessed = std::chrono::steady_clock::now();

    cache->Check();

    cache->cleanup_timer.Enable();
    return true;
}

bool
cache_put(Cache *cache, const char *key,
          CacheItem *item)
{
    /* XXX size constraints */

    assert(item != nullptr);
    assert(item->size > 0);
    assert(item->lock == 0);
    assert(!item->removed);

    if (!cache_need_room(cache, item->size)) {
        item->Destroy();
        return false;
    }

    cache->Check();

    item->key = key;

    auto i = cache->items.find(key, CacheItem::KeyHasher,
                               CacheItem::KeyValueEqual);
    if (i != cache->items.end())
        cache->RemoveItem(*i);

    cache->size += item->size;
    item->last_accessed = std::chrono::steady_clock::now();

    cache->items.insert(*item);
    cache->sorted_items.push_back(*item);

    cache->Check();

    cache->cleanup_timer.Enable();
    return true;
}

bool
cache_put_match(Cache *cache, const char *key,
                CacheItem *item,
                bool (*match)(const CacheItem *, void *),
                void *ctx)
{
    CacheItem *old = cache_get_match(cache, key, match, ctx);

    assert(item != nullptr);
    assert(item->size > 0);
    assert(item->lock == 0);
    assert(!item->removed);

    if (old != nullptr)
        cache->RemoveItem(*old);

    return cache_add(cache, key, item);
}

void
cache_remove(Cache *cache, const char *key)
{
    cache->items.erase_and_dispose(key, CacheItem::KeyHasher,
                                   CacheItem::KeyValueEqual,
                                   [cache](CacheItem *item){
                                       cache->ItemRemoved(item);
                                   });

    cache->Check();
}

void
cache_remove_match(Cache *cache, const char *key,
                   bool (*match)(const CacheItem *, void *),
                   void *ctx)
{
    cache->Check();

    const auto r = cache->items.equal_range(key, CacheItem::KeyHasher,
                                            CacheItem::KeyValueEqual);
    for (auto i = r.first, end = r.second; i != end;) {
        CacheItem &item = *i++;

        if (match(&item, ctx))
            cache->RemoveItem(item);
    }

    cache->Check();
}

void
cache_remove_item(Cache *cache, CacheItem *item)
{
    if (item->removed) {
        /* item has already been removed by somebody else */
        assert(item->lock > 0);
        return;
    }

    cache->RemoveItem(*item);
    cache->Check();
}

unsigned
cache_remove_all_match(Cache *cache,
                       bool (*match)(const CacheItem *, void *),
                       void *ctx)
{
    cache->Check();

    unsigned removed = 0;

    for (auto i = cache->sorted_items.begin(), end = cache->sorted_items.end();
         i != end;) {
        CacheItem &item = *i++;

        if (!match(&item, ctx))
            continue;

        cache->items.erase(cache->items.iterator_to(item));
        cache->ItemRemoved(&item);
        ++removed;
    }

    cache->Check();

    return removed;
}

static std::chrono::steady_clock::time_point
ToSteady(std::chrono::system_clock::time_point t)
{
    const auto now = std::chrono::system_clock::now();
    return t > now
        ? std::chrono::steady_clock::now() + (t - now)
        : std::chrono::steady_clock::time_point();
}

CacheItem::CacheItem(std::chrono::system_clock::time_point _expires,
                     size_t _size)
    :CacheItem(ToSteady(_expires), _size)
{
}

CacheItem::CacheItem(std::chrono::seconds max_age, size_t _size)
    :CacheItem(std::chrono::steady_clock::now() + max_age, _size)
{
}

void
CacheItem::Unlock()
{
    assert(lock > 0);

    if (--lock == 0 && removed)
        /* postponed destroy */
        Destroy();
}

/** clean up expired cache items every 60 seconds */
bool
Cache::ExpireCallback()
{
    const auto now = std::chrono::steady_clock::now();

    Check();

    for (auto i = sorted_items.begin(), end = sorted_items.end(); i != end;) {
        CacheItem &item = *i++;

        if (item.expires > now)
            /* not yet expired */
            continue;

        RemoveItem(item);
    }

    Check();

    return size > 0;
}

void
cache_event_add(Cache *cache)
{
    if (cache->size > 0)
        cache->cleanup_timer.Enable();
}

void
cache_event_del(Cache *cache)
{
    cache->cleanup_timer.Disable();
}
