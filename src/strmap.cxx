/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.hxx"
#include "pool.hxx"

#include <string.h>

inline bool
strmap::Item::Compare::Less(const char *a, const char *b) const
{
    return strcmp(a, b) < 0;
}

strmap::Item *
strmap::Item::Cloner::operator()(const Item &src) const
{
    return NewFromPool<Item>(pool,
                             p_strdup(&pool, src.key),
                             p_strdup(&pool, src.value));
}

strmap::strmap(struct pool &_pool, const strmap &src)
    :pool(_pool) {
    map.clone_from(src.map, Item::Cloner(pool), [](Item *){});
}

void
strmap::Add(const char *key, const char *value)
{
    Item *item = NewFromPool<Item>(pool, key, value);
    map.insert(*item);
}

const char *
strmap::Set(const char *key, const char *value)
{
    const Item item(key, value);
    auto i = map.find(item);
    if (i != map.end()) {
        const char *old_value = i->value;
        i->value = value;
        return old_value;
    } else {
        map.insert(*NewFromPool<Item>(pool, item));
        return nullptr;
    }
}

const char *
strmap::Remove(const char *key)
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    const char *value = i->value;
    map.erase_and_dispose(i, PoolDisposer(pool));
    return value;
}

void
strmap::RemoveAll(const char *key)
{
    map.erase_and_dispose(key, Item::Compare(), PoolDisposer(pool));
}

void
strmap::SecureSet(const char *key, const char *value)
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
        map.insert(r.second, *NewFromPool<Item>(pool, key, value));
}

const char *
strmap::Get(const char *key) const
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    return i->value;
}

std::pair<strmap::const_iterator, strmap::const_iterator>
strmap::EqualRange(const char *key) const
{
    return map.equal_range(key, Item::Compare());
}

struct strmap *
strmap_new(struct pool *pool)
{
    return NewFromPool<struct strmap>(*pool, *pool);
}

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, const struct strmap *src)
{
    return NewFromPool<struct strmap>(*pool, *pool, *src);
}
