/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.hxx"
#include "pool.h"

#include <string.h>

inline bool
strmap::Item::Compare::Less(const char *a, const char *b) const
{
    return strcmp(a, b) < 0;
}

strmap::strmap(struct pool &_pool, const strmap &src)
    :pool(_pool) {
    const auto hint = map.end();
    for (auto &i : src.map) {
        Item *item = NewFromPool<Item>(&pool,
                                       p_strdup(&pool, i.key),
                                       p_strdup(&pool, i.value));
        map.insert(hint, *item);
    }
}

void
strmap::Add(const char *key, const char *value)
{
    Item *item = NewFromPool<Item>(&pool, key, value);
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
        map.insert(*NewFromPool<Item>(&pool, item));
        return nullptr;
    }
}

const char *
strmap::Remove(const char *key)
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    Item *found = &*i;
    map.erase(i);

    const char *value = found->value;
    DeleteFromPool(&pool, found);
    return value;
}

const char *
strmap::Get(const char *key) const
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    return i->value;
}

const struct strmap_pair *
strmap::LookupFirst(const char *key) const
{
    auto i = map.find(key, Item::Compare());
    if (i == map.end())
        return nullptr;

    return &*i;
}

const struct strmap_pair *
strmap::LookupNext(const struct strmap_pair *pair) const
{
    const Item &item = *(const Item *)pair;
    const auto i = std::next(map.iterator_to(item));
    return i != map.end() && strcmp(i->key, pair->key) == 0
        ? &*i
        : nullptr;
}

struct strmap *
strmap_new(struct pool *pool)
{
    return NewFromPool<struct strmap>(pool, *pool);
}

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, struct strmap *src)
{
    return NewFromPool<struct strmap>(pool, *pool, *src);
}
