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
#include "pool/pool.hxx"
#include "util/StringCompare.hxx"
#include "util/StringView.hxx"

#include <iterator>

#include <string.h>

bool
StringMap::Item::Compare::Less(const char *a, const char *b) const noexcept
{
    return strcmp(a, b) < 0;
}

StringMap::Item *
StringMap::Item::Cloner::operator()(const Item &src) const noexcept
{
    return NewFromPool<Item>(pool,
                             p_strdup(&pool, src.key),
                             p_strdup(&pool, src.value));
}

StringMap::Item *
StringMap::Item::ShallowCloner::operator()(const Item &src) const noexcept
{
    return NewFromPool<Item>(pool, ShallowCopy(), src);
}

StringMap::StringMap(struct pool &pool, const StringMap &src) noexcept
{
    map.clone_from(src.map, Item::Cloner(pool), [](Item *){});
}

StringMap::StringMap(struct pool &pool, const StringMap *src) noexcept
{
    if (src != nullptr)
        map.clone_from(src->map, Item::Cloner(pool), [](Item *){});
}

StringMap::StringMap(ShallowCopy, struct pool &pool,
                     const StringMap &src) noexcept
{
    map.clone_from(src.map, Item::ShallowCloner(pool), [](Item *){});
}

void
StringMap::Clear() noexcept
{
    map.clear_and_dispose(NoPoolDisposer());
}

void
StringMap::Add(AllocatorPtr alloc,
               const char *key, const char *value) noexcept
{
    Item *item = alloc.New<Item>(key, value);
    map.insert(*item);
}

const char *
StringMap::Set(AllocatorPtr alloc, const char *key, const char *value) noexcept
{
    auto i = map.upper_bound(key, Item::Compare());
    if (i != map.begin() && strcmp(std::prev(i)->key, key) == 0) {
        --i;
        const char *old_value = i->value;
        i->value = value;
        return old_value;
    } else {
        map.insert_before(i, *alloc.New<Item>(key, value));
        return nullptr;
    }
}

const char *
StringMap::Remove(const char *key) noexcept
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    const char *value = i->value;
    map.erase_and_dispose(i, NoPoolDisposer());
    return value;
}

void
StringMap::RemoveAll(const char *key) noexcept
{
    map.erase_and_dispose(key, Item::Compare(), NoPoolDisposer());
}

void
StringMap::SecureSet(AllocatorPtr alloc,
                     const char *key, const char *value) noexcept
{
    auto r = map.equal_range(key, Item::Compare());
    if (r.first != r.second) {
        if (value != nullptr) {
            /* replace the first value */
            r.first->value = value;
            ++r.first;
        }

        /* and erase all other values with the same key */
        map.erase_and_dispose(r.first, r.second, NoPoolDisposer());
    } else if (value != nullptr)
        map.insert_before(r.second, *alloc.New<Item>(key, value));
}

const char *
StringMap::Get(const char *key) const noexcept
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    return i->value;
}

std::pair<StringMap::const_iterator, StringMap::const_iterator>
StringMap::EqualRange(const char *key) const noexcept
{
    return map.equal_range(key, Item::Compare());
}

void
StringMap::CopyFrom(AllocatorPtr alloc,
                    const StringMap &src, const char *key) noexcept
{
    const auto r = src.EqualRange(key);
    for (auto i = r.first; i != r.second; ++i)
        Add(alloc, key, i->value);
}

void
StringMap::ListCopyFrom(AllocatorPtr alloc,
                        const StringMap &src, const char *const*keys) noexcept
{
    assert(keys != nullptr);

    for (; *keys != nullptr; ++keys)
        CopyFrom(alloc, src, *keys);
}

void
StringMap::PrefixCopyFrom(AllocatorPtr alloc,
                          const StringMap &src, const char *_prefix) noexcept
{
    assert(_prefix != nullptr);
    assert(*_prefix != 0);

    const StringView prefix(_prefix);

    // TODO optimize this search
    for (const auto &i : src)
        if (StringStartsWith(i.key, prefix))
            Add(alloc, i.key, i.value);
}

StringMap *
strmap_new(struct pool *pool) noexcept
{
    return NewFromPool<StringMap>(*pool);
}

StringMap *gcc_malloc
strmap_dup(struct pool *pool, const StringMap *src) noexcept
{
    return NewFromPool<StringMap>(*pool, *pool, *src);
}
