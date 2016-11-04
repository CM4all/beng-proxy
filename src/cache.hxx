/*
 * Generic cache class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CACHE_HXX
#define BENG_PROXY_CACHE_HXX

#include "event/CleanupTimer.hxx"

#include <inline/compiler.h>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

#include <chrono>
#include <memory>

#include <stddef.h>

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

    const std::chrono::steady_clock::time_point expires;

    const size_t size;

    std::chrono::steady_clock::time_point last_accessed =
        std::chrono::steady_clock::time_point();

    /**
     * If non-zero, then this item has been locked by somebody, and
     * must not be destroyed.
     */
    unsigned lock = 0;

    /**
     * If true, then this item has been removed from the cache, but
     * could not be destroyed yet, because it is locked.
     */
    bool removed = false;

    CacheItem(std::chrono::steady_clock::time_point _expires, size_t _size)
        :expires(_expires), size(_size) {}

    CacheItem(std::chrono::system_clock::time_point _expires, size_t _size);
    CacheItem(std::chrono::seconds max_age, size_t _size);

    CacheItem(const CacheItem &) = delete;

    /**
     * Locks the specified item in memory, i.e. prevents that it is
     * freed by Cache::Remove().
     */
    void Lock() {
        ++lock;
    }

    void Unlock();

    virtual bool Validate() const {
        return true;
    }

    virtual void Destroy() = 0;

    gcc_pure
    static size_t KeyHasher(const char *key);

    gcc_pure
    static size_t ValueHasher(const CacheItem &value) {
        return KeyHasher(value.key);
    }

    gcc_pure
    static bool KeyValueEqual(const char *a, const CacheItem &b);

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

class Cache {
    const size_t max_size;
    size_t size = 0;

    typedef boost::intrusive::unordered_multiset<CacheItem,
                                                 boost::intrusive::member_hook<CacheItem,
                                                                               CacheItem::SetHook,
                                                                               &CacheItem::set_hook>,
                                                 boost::intrusive::hash<CacheItem::Hash>,
                                                 boost::intrusive::equal<CacheItem::Equal>,
                                                 boost::intrusive::constant_time_size<false>> ItemSet;

    std::unique_ptr<ItemSet::bucket_type[]> buckets;

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

public:
    Cache(EventLoop &event_loop,
          unsigned hashtable_capacity, size_t _max_size);

    ~Cache();

    void EventAdd();
    void EventDel();

    gcc_pure
    CacheItem *Get(const char *key);

    /**
     * Find the first CacheItem for a key which matches with the
     * specified matching function.
     *
     * @param key the cache item key
     * @param match the match callback function
     * @param ctx a context pointer for the callback
     */
    gcc_pure
    CacheItem *GetMatch(const char *key,
                        bool (*match)(const CacheItem *, void *),
                        void *ctx);

    /**
     * Add an item to this cache.  Item with the same key are preserved.
     *
     * @return false if the item could not be added to the cache due
     * to size constraints
     */
    bool Add(const char *key, CacheItem &item);

    bool Put(const char *key, CacheItem &item);

    /**
     * Adds a new item to this cache, or replaces an existing item
     * which matches with the specified matching function.
     *
     * @param key the cache item key
     * @param item the new cache item
     * @param match the match callback function
     * @param ctx a context pointer for the callback
     */
    bool PutMatch(const char *key, CacheItem &item,
                  bool (*match)(const CacheItem *, void *),
                  void *ctx);

    void Remove(const char *key);

    /**
     * Removes all matching cache items.
     *
     * @return the number of items which were removed
     */
    void RemoveMatch(const char *key,
                     bool (*match)(const CacheItem *, void *), void *ctx);

    void Remove(CacheItem &item);

    /**
     * Removes all matching cache items.
     *
     * @return the number of items which were removed
     */
    unsigned RemoveAllMatch(bool (*match)(const CacheItem *, void *), void *ctx);

    void Flush();

    /** clean up expired cache items every 60 seconds */
    bool ExpireCallback();

    void ItemRemoved(CacheItem *item);

    class ItemRemover {
        Cache &cache;

    public:
        explicit ItemRemover(Cache &_cache):cache(_cache) {}

        void operator()(CacheItem *item) {
            cache.ItemRemoved(item);
        }
    };

    void RemoveItem(CacheItem &item);

    void RefreshItem(CacheItem &item,
                     std::chrono::steady_clock::time_point now);

    void DestroyOldestItem();

    bool NeedRoom(size_t _size);
};

#endif
