/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STRMAP_HXX
#define BENG_PROXY_STRMAP_HXX

#include <inline/compiler.h>

#include <boost/intrusive/set.hpp>

struct pool;

struct strmap_pair {
    const char *key, *value;

    constexpr strmap_pair(const char *_key, const char *_value)
        :key(_key), value(_value) {}
};

struct strmap {
    struct Item : strmap_pair {
        typedef boost::intrusive::set_member_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> Hook;
        Hook hook;

        Item(const char *_key, const char *_value)
            :strmap_pair(_key, _value) {}

        class Compare {
            gcc_pure
            bool Less(const char *a, const char *b) const;

        public:
            gcc_pure
            bool operator()(const char *a, const Item &b) const {
                return Less(a, b.key);
            }

            gcc_pure
            bool operator()(const Item &a, const char *b) const {
                return Less(a.key, b);
            }

            gcc_pure
            bool operator()(const Item &a, const Item &b) const {
                return Less(a.key, b.key);
            }
        };
    };

    struct pool &pool;

    typedef boost::intrusive::multiset<Item,
                                       boost::intrusive::member_hook<Item, Item::Hook, &Item::hook>,
                                       boost::intrusive::compare<Item::Compare>,
                                       boost::intrusive::constant_time_size<false>> Map;

    typedef Map::const_iterator const_iterator;

    Map map;

    explicit strmap(struct pool &_pool):pool(_pool) {}

    strmap(struct pool &_pool, const strmap &src);

    strmap(const strmap &) = delete;

    const_iterator begin() const {
        return map.begin();
    }

    const_iterator end() const {
        return map.end();
    }

    void Add(const char *key, const char *value);
    const char *Set(const char *key, const char *value);
    const char *Remove(const char *key);

    gcc_pure
    const char *Get(const char *key) const;

    gcc_pure
    const struct strmap_pair *LookupFirst(const char *key) const;

    gcc_pure
    const struct strmap_pair *LookupNext(const struct strmap_pair *pair) const;
};

struct strmap *gcc_malloc
strmap_new(struct pool *pool);

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, struct strmap *src);

static inline const char *
strmap_get(const struct strmap *map, const char *key)
{
    return map->Get(key);
}

/**
 * @see hashmap_lookup_first()
 */
static inline const struct strmap_pair *
strmap_lookup_first(const struct strmap *map, const char *key)
{
    return map->LookupFirst(key);
}

/**
 * @see hashmap_lookup_next()
 */
static inline const struct strmap_pair *
strmap_lookup_next(const struct strmap *map, const struct strmap_pair *pair)
{
    return map->LookupNext(pair);
}

/**
 * This variation of strmap::Remove() allows the caller to pass
 * map=nullptr.
 */
static inline const char *
strmap_remove_checked(struct strmap *map, const char *key)
{
    return map != nullptr
        ? map->Remove(key)
        : nullptr;
}

/**
 * This variation of strmap_get() allows the caller to pass map=nullptr.
 */
gcc_pure
static inline const char *
strmap_get_checked(const struct strmap *map, const char *key)
{
    return map != nullptr
        ? strmap_get(map, key)
        : nullptr;
}

#endif
