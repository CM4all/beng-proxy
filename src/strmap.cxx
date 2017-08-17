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

#include "strmap.hxx"
#include "pool.hxx"

#include <iterator>

#include <string.h>

bool
StringMap::Item::Compare::Less(const char *a, const char *b) const
{
    return strcmp(a, b) < 0;
}

StringMap::Item *
StringMap::Item::Cloner::operator()(const Item &src) const
{
    return NewFromPool<Item>(pool,
                             p_strdup(&pool, src.key),
                             p_strdup(&pool, src.value));
}

StringMap::Item *
StringMap::Item::ShallowCloner::operator()(const Item &src) const
{
    return NewFromPool<Item>(pool, ShallowCopy(), src);
}

StringMap::StringMap(struct pool &_pool, const StringMap &src)
    :pool(_pool) {
    map.clone_from(src.map, Item::Cloner(pool), [](Item *){});
}

StringMap::StringMap(struct pool &_pool, const StringMap *src)
    :pool(_pool) {
    if (src != nullptr)
        map.clone_from(src->map, Item::Cloner(pool), [](Item *){});
}

StringMap::StringMap(ShallowCopy, struct pool &_pool, const StringMap &src)
    :pool(_pool)
{
    map.clone_from(src.map, Item::ShallowCloner(pool), [](Item *){});
}

void
StringMap::Clear()
{
    map.clear_and_dispose(PoolDisposer(pool));
}

void
StringMap::Add(const char *key, const char *value)
{
    Item *item = NewFromPool<Item>(pool, key, value);
    map.insert(*item);
}

const char *
StringMap::Set(const char *key, const char *value)
{
    auto i = map.upper_bound(key, Item::Compare());
    if (i != map.begin() && strcmp(std::prev(i)->key, key) == 0) {
        --i;
        const char *old_value = i->value;
        i->value = value;
        return old_value;
    } else {
        map.insert_before(i, *NewFromPool<Item>(pool, key, value));
        return nullptr;
    }
}

const char *
StringMap::Remove(const char *key)
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    const char *value = i->value;
    map.erase_and_dispose(i, PoolDisposer(pool));
    return value;
}

void
StringMap::RemoveAll(const char *key)
{
    map.erase_and_dispose(key, Item::Compare(), PoolDisposer(pool));
}

void
StringMap::SecureSet(const char *key, const char *value)
{
    auto r = map.equal_range(key, Item::Compare());
    if (r.first != r.second) {
        if (value != nullptr) {
            /* replace the first value */
            r.first->value = value;
            ++r.first;
        }

        /* and erase all other values with the same key */
        map.erase_and_dispose(r.first, r.second, PoolDisposer(pool));
    } else if (value != nullptr)
        map.insert_before(r.second, *NewFromPool<Item>(pool, key, value));
}

const char *
StringMap::Get(const char *key) const
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    return i->value;
}

std::pair<StringMap::const_iterator, StringMap::const_iterator>
StringMap::EqualRange(const char *key) const
{
    return map.equal_range(key, Item::Compare());
}

StringMap *
strmap_new(struct pool *pool)
{
    return NewFromPool<StringMap>(*pool, *pool);
}

StringMap *gcc_malloc
strmap_dup(struct pool *pool, const StringMap *src)
{
    return NewFromPool<StringMap>(*pool, *pool, *src);
}
