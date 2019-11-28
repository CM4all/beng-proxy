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

#ifndef BENG_PROXY_STRMAP_HXX
#define BENG_PROXY_STRMAP_HXX

#include "util/ShallowCopy.hxx"
#include "util/Compiler.h"
#include "AllocatorPtr.hxx"

#include <boost/intrusive/set.hpp>

#include <utility>

struct pool;

/**
 * String hash map.
 */
class StringMap {
    struct Item : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        const char *key, *value;

        Item(const char *_key, const char *_value) noexcept
            :key(_key), value(_value) {}

        Item(ShallowCopy, const Item &src) noexcept
            :key(src.key), value(src.value) {}

        Item(const Item &) = delete;
        Item &operator=(const Item &) = delete;

        class Compare {
            gcc_pure
            bool Less(const char *a, const char *b) const noexcept;

        public:
            gcc_pure
            bool operator()(const char *a, const Item &b) const noexcept {
                return Less(a, b.key);
            }

            gcc_pure
            bool operator()(const Item &a, const char *b) const noexcept {
                return Less(a.key, b);
            }

            gcc_pure
            bool operator()(const Item &a, const Item &b) const noexcept {
                return Less(a.key, b.key);
            }
        };

        class Cloner {
            struct pool &pool;

        public:
            explicit Cloner(struct pool &_pool) noexcept:pool(_pool) {}

            Item *operator()(const Item &src) const noexcept;
        };

        class ShallowCloner {
            struct pool &pool;

        public:
            explicit ShallowCloner(struct pool &_pool) noexcept:pool(_pool) {}

            Item *operator()(const Item &src) const noexcept;
        };
    };

    typedef boost::intrusive::multiset<Item,
                                       boost::intrusive::compare<Item::Compare>,
                                       boost::intrusive::constant_time_size<false>> Map;

    typedef Map::const_iterator const_iterator;

    Map map;

public:
    StringMap() = default;

    template<typename A>
    explicit StringMap(A &&_alloc,
                       std::initializer_list<std::pair<const char *, const char *>> init) noexcept
    {
        for (const auto &i : init)
            Add(_alloc, i.first, i.second);
    }

    StringMap(struct pool &_pool, const StringMap &src) noexcept;
    StringMap(struct pool &_pool, const StringMap *src) noexcept;

    /**
     * Copy string pointers from #src.
     */
    StringMap(ShallowCopy, struct pool &_pool, const StringMap &src) noexcept;

    StringMap(const StringMap &) = delete;

    StringMap(StringMap &&src) = default;

    /**
     * Move-assign all items.  Note that this does not touch the pool;
     * this operation is only safe if both instances use the same
     * pool.
     */
    StringMap &operator=(StringMap &&src) noexcept {
        map.swap(src.map);
        return *this;
    }

    const_iterator begin() const noexcept {
        return map.begin();
    }

    const_iterator end() const noexcept {
        return map.end();
    }

    gcc_pure
    bool IsEmpty() const noexcept {
        return map.empty();
    }

    void Clear() noexcept;

    void Add(AllocatorPtr alloc, const char *key, const char *value) noexcept;
    const char *Set(AllocatorPtr alloc,
                    const char *key, const char *value) noexcept;
    const char *Remove(const char *key) noexcept;

    /**
     * Remove all values with the specified key.
     */
    void RemoveAll(const char *key) noexcept;

    /**
     * Remove all existing values with the specified key and
     * (optionally, if not nullptr) add a new value.
     */
    void SecureSet(AllocatorPtr alloc,
                   const char *key, const char *value) noexcept;

    gcc_pure
    const char *Get(const char *key) const noexcept;

    gcc_pure
    bool Contains(const char *key) const noexcept {
        return Get(key) != nullptr;
    }

    gcc_pure
    std::pair<const_iterator, const_iterator> EqualRange(const char *key) const noexcept;

    void CopyFrom(AllocatorPtr alloc,
                  const StringMap &src, const char *key) noexcept;

    /**
     * Copy string pointers with keys from the given key list.
     *
     * @param keys a nullptr terminated array of keys
     */
    void ListCopyFrom(AllocatorPtr alloc,
                      const StringMap &src, const char *const*keys) noexcept;

    /**
     * Copy string pointers with the given key prefix.
     *
     * @param keys a nullptr terminated array of keys
     */
    void PrefixCopyFrom(AllocatorPtr alloc,
                        const StringMap &src, const char *prefix) noexcept;

    /**
     * Move items from #src, merging it into this object.
     */
    void Merge(StringMap &&src) noexcept {
        src.map.clear_and_dispose([this](Item *item){
                map.insert(*item);
            });
    }
};

StringMap *gcc_malloc
strmap_new(struct pool *pool) noexcept;

StringMap *gcc_malloc
strmap_dup(struct pool *pool, const StringMap *src) noexcept;

/**
 * This variation of StringMap::Get() allows the caller to pass
 * map=nullptr.
 */
gcc_pure
static inline const char *
strmap_get_checked(const StringMap *map, const char *key) noexcept
{
    return map != nullptr
        ? map->Get(key)
        : nullptr;
}

#endif
