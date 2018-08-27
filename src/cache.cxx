/*
 * Copyright 2007-2017 Content Management AG
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

#include "cache.hxx"
#include "util/djbhash.h"

#include <assert.h>
#include <string.h>

inline size_t
CacheItem::KeyHasher(const char *key)
{
    assert(key != nullptr);

    return djb_hash_string(key);
}

bool
CacheItem::KeyValueEqual(const char *a, const CacheItem &b)
{
    assert(a != nullptr);

    return strcmp(a, b.key) == 0;
}

void
CacheItem::Release()
{
    if (lock == 0)
        Destroy();
    else
        /* this item is locked - postpone the Destroy() call */
        removed = true;
}

Cache::Cache(EventLoop &event_loop,
             unsigned hashtable_capacity, size_t _max_size)
    :max_size(_max_size),
     buckets(new ItemSet::bucket_type[hashtable_capacity]),
     items(ItemSet::bucket_traits(buckets.get(), hashtable_capacity)),
     cleanup_timer(event_loop, std::chrono::minutes(1),
                   BIND_THIS_METHOD(ExpireCallback)) {}

Cache::~Cache()
{
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
Cache::ItemRemoved(CacheItem *item)
{
    assert(item != nullptr);
    assert(item->size > 0);
    assert(item->lock > 0 || !item->removed);
    assert(size >= item->size);

    sorted_items.erase(sorted_items.iterator_to(*item));

    size -= item->size;

    item->Release();

    if (size == 0)
        cleanup_timer.Disable();
}

void
Cache::Flush()
{
    items.clear_and_dispose(Cache::ItemRemover(*this));
}

static bool
cache_item_validate(CacheItem *item,
                    std::chrono::steady_clock::time_point now)
{
    return now < item->expires && item->Validate();
}

void
Cache::RefreshItem(CacheItem &item,
                   std::chrono::steady_clock::time_point now)
{
    item.last_accessed = now;

    /* move to the front of the linked list */
    sorted_items.erase(sorted_items.iterator_to(item));
    sorted_items.push_back(item);
}

void
Cache::RemoveItem(CacheItem &item)
{
    assert(!item.removed);

    items.erase_and_dispose(items.iterator_to(item),
                            ItemRemover(*this));
}

CacheItem *
Cache::Get(const char *key)
{
    auto i = items.find(key, CacheItem::KeyHasher, CacheItem::KeyValueEqual);
    if (i == items.end())
        return nullptr;

    CacheItem *item = &*i;

    const auto now = std::chrono::steady_clock::now();

    if (!cache_item_validate(item, now)) {
        RemoveItem(*item);
        return nullptr;
    }

    RefreshItem(*item, now);
    return item;
}

CacheItem *
Cache::GetMatch(const char *key,
                bool (*match)(const CacheItem *, void *),
                void *ctx)
{
    const auto now = std::chrono::steady_clock::now();

    const auto r = items.equal_range(key, CacheItem::KeyHasher,
                                     CacheItem::KeyValueEqual);
    for (auto i = r.first, end = r.second; i != end;) {
        CacheItem *item = &*i++;

        if (!cache_item_validate(item, now)) {
            /* expired cache item: delete it, and re-start the
               search */

            RemoveItem(*item);
        } else if (match(item, ctx)) {
            /* this one matches: return it to the caller */
            RefreshItem(*item, now);
            return item;
        }
    };

    return nullptr;
}

void
Cache::DestroyOldestItem()
{
    if (sorted_items.empty())
        return;

    CacheItem &item = sorted_items.front();
    RemoveItem(item);
}

bool
Cache::NeedRoom(size_t _size)
{
    if (_size > max_size)
        return false;

    while (true) {
        if (size + _size <= max_size)
            return true;

        DestroyOldestItem();
    }
}

bool
Cache::Add(const char *key, CacheItem &item)
{
    /* XXX size constraints */
    if (!NeedRoom(item.size)) {
        item.Destroy();
        return false;
    }

    item.key = key;
    items.insert(item);
    sorted_items.push_back(item);

    size += item.size;
    item.last_accessed = std::chrono::steady_clock::now();

    cleanup_timer.Enable();
    return true;
}

bool
Cache::Put(const char *key, CacheItem &item)
{
    /* XXX size constraints */

    assert(item.size > 0);
    assert(item.lock == 0);
    assert(!item.removed);

    if (!NeedRoom(item.size)) {
        item.Destroy();
        return false;
    }

    item.key = key;

    auto i = items.find(key, CacheItem::KeyHasher,
                        CacheItem::KeyValueEqual);
    if (i != items.end())
        RemoveItem(*i);

    size += item.size;
    item.last_accessed = std::chrono::steady_clock::now();

    items.insert(item);
    sorted_items.push_back(item);

    cleanup_timer.Enable();
    return true;
}

bool
Cache::PutMatch(const char *key, CacheItem &item,
                bool (*match)(const CacheItem *, void *), void *ctx)
{
    auto *old = GetMatch(key, match, ctx);

    assert(item.size > 0);
    assert(item.lock == 0);
    assert(!item.removed);

    if (old != nullptr)
        RemoveItem(*old);

    return Add(key, item);
}

void
Cache::Remove(const char *key)
{
    items.erase_and_dispose(key, CacheItem::KeyHasher,
                            CacheItem::KeyValueEqual,
                            [this](CacheItem *item){
                                ItemRemoved(item);
                            });
}

void
Cache::RemoveMatch(const char *key,
                   bool (*match)(const CacheItem *, void *), void *ctx)
{
    const auto r = items.equal_range(key, CacheItem::KeyHasher,
                                     CacheItem::KeyValueEqual);
    for (auto i = r.first, end = r.second; i != end;) {
        CacheItem &item = *i++;

        if (match(&item, ctx))
            RemoveItem(item);
    }
}

void
Cache::Remove(CacheItem &item)
{
    if (item.removed) {
        /* item has already been removed by somebody else */
        assert(item.lock > 0);
        return;
    }

    RemoveItem(item);
}

unsigned
Cache::RemoveAllMatch(bool (*match)(const CacheItem *, void *), void *ctx)
{
    unsigned removed = 0;

    for (auto i = sorted_items.begin(), end = sorted_items.end();
         i != end;) {
        CacheItem &item = *i++;

        if (!match(&item, ctx))
            continue;

        items.erase(items.iterator_to(item));
        ItemRemoved(&item);
        ++removed;
    }

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

    for (auto i = sorted_items.begin(), end = sorted_items.end(); i != end;) {
        CacheItem &item = *i++;

        if (item.expires > now)
            /* not yet expired */
            continue;

        RemoveItem(item);
    }

    return size > 0;
}

void
Cache::EventAdd()
{
    if (size > 0)
        cleanup_timer.Enable();
}

void
Cache::EventDel()
{
    cleanup_timer.Disable();
}
