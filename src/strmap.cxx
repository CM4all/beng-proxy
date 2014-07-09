/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.hxx"
#include "pool.h"

#include <boost/intrusive/set.hpp>

#include <assert.h>
#include <string.h>

struct strmap {
    struct Item : strmap_pair {
        typedef boost::intrusive::set_member_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> Hook;
        Hook hook;

        Item(const char *_key, const char *_value)
            :strmap_pair(_key, _value) {}

        struct Compare {
            gcc_pure
            bool operator()(const char *a, const Item &b) const {
                return strcmp(a, b.key) < 0;
            }

            gcc_pure
            bool operator()(const Item &a, const char *b) const {
                return strcmp(a.key, b) < 0;
            }

            gcc_pure
            bool operator()(const Item &a, const Item &b) const {
                return strcmp(a.key, b.key) < 0;
            }
        };
    };

    struct pool &pool;

    typedef boost::intrusive::multiset<Item,
                                       boost::intrusive::member_hook<Item, Item::Hook, &Item::hook>,
                                       boost::intrusive::compare<Item::Compare>,
                                       boost::intrusive::constant_time_size<false>> Map;
    Map map;

    mutable Map::const_iterator cursor;

    explicit strmap(struct pool &_pool):pool(_pool) {}

    strmap(struct pool &_pool, const strmap &src):pool(_pool) {
        const auto hint = map.end();
        for (auto &i : src.map) {
            Item *item = NewFromPool<Item>(&pool,
                                           p_strdup(&pool, i.key),
                                           p_strdup(&pool, i.value));
            map.insert(hint, *item);
        }
    }

    strmap(const strmap &) = delete;

    void Add(const char *key, const char *value) {
        Item *item = NewFromPool<Item>(&pool, key, value);
        map.insert(*item);
    }

    const char *Set(const char *key, const char *value) {
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

    const char *Remove(const char *key) {
        auto i = map.find(key, Item::Compare());
        if (i == map.end())
            return nullptr;

        Item *found = &*i;
        map.erase(i);

        const char *value = found->value;
        DeleteFromPool(&pool, found);
        return value;
    }

    const char *Get(const char *key) const {
        auto i = map.find(key, Item::Compare());
        if (i == map.end())
            return nullptr;

        return i->value;
    }

    const struct strmap_pair *LookupFirst(const char *key) const {
        auto i = map.find(key, Item::Compare());
        if (i == map.end())
            return nullptr;

        return &*i;
    }

    const struct strmap_pair *LookupNext(const struct strmap_pair *pair) const {
        const Item &item = *(const Item *)pair;
        const auto i = std::next(map.iterator_to(item));
        return i != map.end() && strcmp(i->key, pair->key) == 0
            ? &*i
            : nullptr;
    }
};

struct strmap *
strmap_new(struct pool *pool, gcc_unused unsigned capacity)
{
    return NewFromPool<struct strmap>(pool, *pool);
}

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, struct strmap *src, gcc_unused unsigned capacity)
{
    return NewFromPool<struct strmap>(pool, *pool, *src);
}

void
strmap_add(struct strmap *map, const char *key, const char *value)
{
    map->Add(key, value);
}

const char *
strmap_set(struct strmap *map, const char *key, const char *value)
{
    return map->Set(key, value);
}

const char *
strmap_remove(struct strmap *map, const char *key)
{
    return map->Remove(key);
}

const char *
strmap_get(const struct strmap *map, const char *key)
{
    return map->Get(key);
}

const struct strmap_pair *
strmap_lookup_first(const struct strmap *map, const char *key)
{
    return map->LookupFirst(key);
}

const struct strmap_pair *
strmap_lookup_next(const struct strmap *map, const struct strmap_pair *pair)
{
    return map->LookupNext(pair);
}

void
strmap_rewind(struct strmap *map)
{
    assert(map != nullptr);

    map->cursor = map->map.begin();
}

const struct strmap_pair *
strmap_next(struct strmap *map)
{
    assert(map != nullptr);

    if (map->cursor == map->map.end())
        return nullptr;

    return &*map->cursor++;
}
