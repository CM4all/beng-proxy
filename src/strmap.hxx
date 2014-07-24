/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STRMAP_HXX
#define BENG_PROXY_STRMAP_HXX

#include <inline/compiler.h>

#include <boost/intrusive/set.hpp>

#include <utility>

struct pool;

struct strmap {
    struct Item : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
        const char *key, *value;

        Item(const char *_key, const char *_value)
            :key(_key), value(_value) {}

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
                                       boost::intrusive::compare<Item::Compare>,
                                       boost::intrusive::constant_time_size<false>> Map;

    typedef Map::const_iterator const_iterator;

    Map map;

    explicit strmap(struct pool &_pool):pool(_pool) {}

    strmap(struct pool &_pool, const strmap &src);

    strmap(const strmap &) = delete;

    strmap(strmap &&src) = default;

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
    std::pair<const_iterator, const_iterator> EqualRange(const char *key) const;
};

struct strmap *gcc_malloc
strmap_new(struct pool *pool);

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, const struct strmap *src);

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
 * This variation of strmap::Get() allows the caller to pass
 * map=nullptr.
 */
gcc_pure
static inline const char *
strmap_get_checked(const struct strmap *map, const char *key)
{
    return map != nullptr
        ? map->Get(key)
        : nullptr;
}

#endif
