/*
 * Copyright 2007-2018 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_CACHE_HXX
#define BENG_PROXY_CACHE_HXX

#include "event/CleanupTimer.hxx"

#include "util/Compiler.h"

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

    CacheItem(std::chrono::steady_clock::time_point _expires,
              size_t _size) noexcept
        :expires(_expires), size(_size) {}

    CacheItem(std::chrono::steady_clock::time_point now,
              std::chrono::system_clock::time_point _expires,
              size_t _size) noexcept;

    CacheItem(std::chrono::steady_clock::time_point now,
              std::chrono::seconds max_age, size_t _size) noexcept;

    CacheItem(const CacheItem &) = delete;

    void Release() noexcept;

    /**
     * Locks the specified item in memory, i.e. prevents that it is
     * freed by Cache::Remove().
     */
    void Lock() noexcept {
        ++lock;
    }

    void Unlock() noexcept;

    virtual bool Validate() const noexcept {
        return true;
    }

    virtual void Destroy() noexcept = 0;

    gcc_pure
    static size_t KeyHasher(const char *key) noexcept;

    gcc_pure
    static size_t ValueHasher(const CacheItem &value) noexcept {
        return KeyHasher(value.key);
    }

    gcc_pure
    static bool KeyValueEqual(const char *a, const CacheItem &b) noexcept;

    struct Hash {
        gcc_pure
        size_t operator()(const CacheItem &value) const noexcept {
            return ValueHasher(value);
        }
    };

    struct Equal {
        gcc_pure
        bool operator()(const CacheItem &a,
                        const CacheItem &b) const noexcept {
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
          unsigned hashtable_capacity, size_t _max_size) noexcept;

    ~Cache() noexcept;

    auto &GetEventLoop() const noexcept {
        return cleanup_timer.GetEventLoop();
    }

    gcc_pure
    std::chrono::steady_clock::time_point SteadyNow() const noexcept;

    void EventAdd() noexcept;
    void EventDel() noexcept;

    gcc_pure
    CacheItem *Get(const char *key) noexcept;

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
                        void *ctx) noexcept;

    /**
     * Add an item to this cache.  Item with the same key are preserved.
     *
     * @return false if the item could not be added to the cache due
     * to size constraints
     */
    bool Add(const char *key, CacheItem &item) noexcept;

    bool Put(const char *key, CacheItem &item) noexcept;

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
                  void *ctx) noexcept;

    void Remove(const char *key) noexcept;

    /**
     * Removes all matching cache items.
     *
     * @return the number of items which were removed
     */
    void RemoveMatch(const char *key,
                     bool (*match)(const CacheItem *, void *),
                     void *ctx) noexcept;

    void Remove(CacheItem &item) noexcept;

    /**
     * Removes all matching cache items.
     *
     * @return the number of items which were removed
     */
    unsigned RemoveAllMatch(bool (*match)(const CacheItem *, void *),
                            void *ctx) noexcept;

    void Flush() noexcept;

private:
    /** clean up expired cache items every 60 seconds */
    bool ExpireCallback() noexcept;

    void ItemRemoved(CacheItem *item) noexcept;

    class ItemRemover {
        Cache &cache;

    public:
        explicit constexpr ItemRemover(Cache &_cache) noexcept
            :cache(_cache) {}

        void operator()(CacheItem *item) noexcept {
            cache.ItemRemoved(item);
        }
    };

    void RemoveItem(CacheItem &item) noexcept;

    void RefreshItem(CacheItem &item,
                     std::chrono::steady_clock::time_point now) noexcept;

    void DestroyOldestItem() noexcept;

    bool NeedRoom(size_t _size) noexcept;
};

#endif
