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

struct StringMap {
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

        class Cloner {
            struct pool &pool;

        public:
            explicit Cloner(struct pool &_pool):pool(_pool) {}

            Item *operator()(const Item &src) const;
        };
    };

    struct pool &pool;

    typedef boost::intrusive::multiset<Item,
                                       boost::intrusive::compare<Item::Compare>,
                                       boost::intrusive::constant_time_size<false>> Map;

    typedef Map::const_iterator const_iterator;

    Map map;

    explicit StringMap(struct pool &_pool):pool(_pool) {}

    StringMap(struct pool &_pool, const StringMap &src);

    StringMap(const StringMap &) = delete;

    StringMap(StringMap &&src) = default;

    struct pool &GetPool() {
        return pool;
    }

    const_iterator begin() const {
        return map.begin();
    }

    const_iterator end() const {
        return map.end();
    }

    gcc_pure
    bool IsEmpty() const {
        return map.empty();
    }

    void Add(const char *key, const char *value);
    const char *Set(const char *key, const char *value);
    const char *Remove(const char *key);

    /**
     * Remove all values with the specified key.
     */
    void RemoveAll(const char *key);

    /**
     * Remove all existing values with the specified key and
     * (optionally, if not nullptr) add a new value.
     */
    void SecureSet(const char *key, const char *value);

    gcc_pure
    const char *Get(const char *key) const;

    gcc_pure
    bool Contains(const char *key) const {
        return Get(key) != nullptr;
    }

    gcc_pure
    std::pair<const_iterator, const_iterator> EqualRange(const char *key) const;
};

StringMap *gcc_malloc
strmap_new(struct pool *pool);

StringMap *gcc_malloc
strmap_dup(struct pool *pool, const StringMap *src);

/**
 * This variation of StringMap::Remove() allows the caller to pass
 * map=nullptr.
 */
static inline const char *
strmap_remove_checked(StringMap *map, const char *key)
{
    return map != nullptr
        ? map->Remove(key)
        : nullptr;
}

/**
 * This variation of StringMap::Get() allows the caller to pass
 * map=nullptr.
 */
gcc_pure
static inline const char *
strmap_get_checked(const StringMap *map, const char *key)
{
    return map != nullptr
        ? map->Get(key)
        : nullptr;
}

#endif
