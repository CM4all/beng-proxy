/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CACHE_HXX
#define BENG_PROXY_CACHE_HXX

#include <inline/compiler.h>

#include <boost/intrusive/list.hpp>

#include <sys/time.h>
#include <stddef.h>

struct pool;
struct cache;
struct AllocatorStats;

struct cache_item {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;

    /**
     * This item's siblings, sorted by #last_accessed.
     */
    SiblingsHook sorted_siblings;

    /**
     * The key under which this item is stored in the hash table.
     */
    const char *key;

    unsigned expires;
    size_t size;
    unsigned last_accessed;

    /**
     * If non-zero, then this item has been locked by somebody, and
     * must not be destroyed.
     */
    unsigned lock;

    /**
     * If true, then this item has been removed from the cache, but
     * could not be destroyed yet, because it is locked.
     */
    bool removed;

    cache_item() = default;
    cache_item(const cache_item &) = delete;
};

struct cache_class {
    bool (*validate)(struct cache_item *item);
    void (*destroy)(struct cache_item *item);
};

gcc_malloc
struct cache *
cache_new(struct pool &pool, const struct cache_class *cls,
          unsigned hashtable_capacity, size_t max_size);

void
cache_close(struct cache *cache);

/**
 * Obtain statistics.
 */
gcc_pure
AllocatorStats
cache_get_stats(const struct cache &cache);

void
cache_flush(struct cache *cache);

gcc_pure
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
gcc_pure
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

/**
 * Removes all matching cache items.
 *
 * @return the number of items which were removed
 */
void
cache_remove_match(struct cache *cache, const char *key,
                   bool (*match)(const struct cache_item *, void *),
                   void *ctx);

void
cache_remove_item(struct cache *cache, const char *key,
                  struct cache_item *item);

/**
 * Removes all matching cache items.
 *
 * @return the number of items which were removed
 */
unsigned
cache_remove_all_match(struct cache *cache,
                       bool (*match)(const struct cache_item *, void *),
                       void *ctx);

/**
 * Initializes the specified #cache_item.  You should not manually
 * initialize an item, because you won't notice API changes then.
 */
static inline void
cache_item_init(struct cache_item *item, unsigned expires, size_t size)
{
    item->expires = expires;
    item->size = size;
    item->last_accessed = 0;
    item->lock = 0;
    item->removed = false;
}

void
cache_item_init_absolute(struct cache_item *item, time_t expires, size_t size);

void
cache_item_init_relative(struct cache_item *item, unsigned max_age,
                         size_t size);

/**
 * Locks the specified item in memory, i.e. prevents that it is freed
 * by cache_remove().
 */
void
cache_item_lock(struct cache_item *item);

void
cache_item_unlock(struct cache *cache, struct cache_item *item);

void
cache_event_add(struct cache *cache);

void
cache_event_del(struct cache *cache);

#endif
