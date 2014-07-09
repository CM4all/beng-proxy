/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STRMAP_HXX
#define BENG_PROXY_STRMAP_HXX

#include <inline/compiler.h>

struct pool;

struct strmap_pair {
    const char *key, *value;

    constexpr strmap_pair(const char *_key, const char *_value)
        :key(_key), value(_value) {}
};

struct strmap *gcc_malloc
strmap_new(struct pool *pool);

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, struct strmap *src);

void
strmap_add(struct strmap *map, const char *key, const char *value);

const char *
strmap_set(struct strmap *map, const char *key, const char *value);

const char *
strmap_remove(struct strmap *map, const char *key);

gcc_pure
const char *
strmap_get(const struct strmap *map, const char *key);

/**
 * @see hashmap_lookup_first()
 */
gcc_pure
const struct strmap_pair *
strmap_lookup_first(const struct strmap *map, const char *key);

/**
 * @see hashmap_lookup_next()
 */
gcc_pure
const struct strmap_pair *
strmap_lookup_next(const struct strmap *map, const struct strmap_pair *pair);

void
strmap_rewind(struct strmap *map);

const struct strmap_pair *
strmap_next(struct strmap *map);

/**
 * This variation of strmap_remove() allows the caller to pass map=nullptr.
 */
static inline const char *
strmap_remove_checked(struct strmap *map, const char *key)
{
    return map != nullptr
        ? strmap_remove(map, key)
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
