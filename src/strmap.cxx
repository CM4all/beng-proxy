/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.hxx"
#include "pool.hxx"

#include <string.h>

inline bool
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

StringMap::StringMap(ShallowCopy, struct pool &_pool, const StringMap &src)
    :pool(_pool)
{
    map.clone_from(src.map, Item::ShallowCloner(pool), [](Item *){});
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
    if (i != map.end() && strcmp(i->key, key) == 0) {
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
