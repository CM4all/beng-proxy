/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CACHE_HXX
#define BENG_PROXY_CACHE_HXX

#include <inline/compiler.h>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <chrono>

#include <sys/time.h>
#include <stddef.h>
#include <string.h>

struct pool;
struct Cache;
struct AllocatorStats;
class EventLoop;

struct CacheItem {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    typedef boost::intrusive::unordered_set_member_hook<LinkMode> SetHook;

    /**
     * This item's siblings, sorted by #last_accessed.
     */
    SiblingsHook sorted_siblings;

    SetHook set_hook;

    /**
     * The key under which this item is stored in the hash table.
     */
    const char *key;

    std::chrono::steady_clock::time_point expires;

    size_t size;
    std::chrono::steady_clock::time_point last_accessed;

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

    CacheItem() = default;
    CacheItem(const CacheItem &) = delete;

    void Init(std::chrono::steady_clock::time_point _expires,
              size_t _size) {
        expires = _expires;
        size = _size;
        last_accessed = std::chrono::steady_clock::time_point();
        lock = 0;
        removed = false;
    }

    void InitAbsolute(time_t _expires, size_t size);
    void InitRelative(unsigned max_age, size_t size);

    gcc_pure
    static size_t KeyHasher(const char *key);

    gcc_pure
    static size_t ValueHasher(const CacheItem &value) {
        return KeyHasher(value.key);
    }

    gcc_pure
    static bool KeyValueEqual(const char *a, const CacheItem &b) {
        assert(a != nullptr);

        return strcmp(a, b.key) == 0;
    }

    struct Hash {
        gcc_pure
        size_t operator()(const CacheItem &value) const {
            return ValueHasher(value);
        }
    };

    struct Equal {
        gcc_pure
        bool operator()(const CacheItem &a,
                        const CacheItem &b) const {
            return KeyValueEqual(a.key, b);
        }
    };
};

struct CacheClass {
    bool (*validate)(CacheItem *item);
    void (*destroy)(CacheItem *item);
};

gcc_malloc
Cache *
cache_new(struct pool &pool, EventLoop &event_loop,
          const CacheClass &cls,
          unsigned hashtable_capacity, size_t max_size);

void
cache_close(Cache *cache);

/**
 * Obtain statistics.
 */
gcc_pure
AllocatorStats
cache_get_stats(const Cache &cache);

void
cache_flush(Cache *cache);

gcc_pure
CacheItem *
cache_get(Cache *cache, const char *key);

/**
 * Find the first CacheItem for a key which matches with the
 * specified matching function.
 *
 * @param cache the cache object
 * @param key the cache item key
 * @param match the match callback function
 * @param ctx a context pointer for the callback
 */
gcc_pure
CacheItem *
cache_get_match(Cache *cache, const char *key,
                bool (*match)(const CacheItem *, void *),
                void *ctx);

/**
 * Add an item to this cache.  Item with the same key are preserved.
 *
 * @return false if the item could not be added to the cache due to
 * size constraints
 */
bool
cache_add(Cache *cache, const char *key,
          CacheItem *item);

bool
cache_put(Cache *cache, const char *key,
          CacheItem *item);

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
bool
cache_put_match(Cache *cache, const char *key,
                CacheItem *item,
                bool (*match)(const CacheItem *, void *),
                void *ctx);

void
cache_remove(Cache *cache, const char *key);

/**
 * Removes all matching cache items.
 *
 * @return the number of items which were removed
 */
void
cache_remove_match(Cache *cache, const char *key,
                   bool (*match)(const CacheItem *, void *),
                   void *ctx);

void
cache_remove_item(Cache *cache, CacheItem *item);

/**
 * Removes all matching cache items.
 *
 * @return the number of items which were removed
 */
unsigned
cache_remove_all_match(Cache *cache,
                       bool (*match)(const CacheItem *, void *),
                       void *ctx);

/**
 * Locks the specified item in memory, i.e. prevents that it is freed
 * by cache_remove().
 */
void
cache_item_lock(CacheItem *item);

void
cache_item_unlock(Cache *cache, CacheItem *item);

void
cache_event_add(Cache *cache);

void
cache_event_del(Cache *cache);

#endif
