/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.hxx"
#include "hashmap.hxx"
#include "pool.h"

#include <assert.h>

struct strmap {
    struct hashmap *hashmap;
};

struct strmap *
strmap_new(struct pool *pool, unsigned capacity)
{
    auto map = NewFromPool<struct strmap>(pool);
    assert(capacity > 1);

    map->hashmap = hashmap_new(pool, capacity);

    return map;
}

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, struct strmap *src, unsigned capacity)
{
    struct strmap *dest = strmap_new(pool, capacity);
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != nullptr)
        strmap_add(dest, p_strdup(pool, pair->key),
                   p_strdup(pool, pair->value));

    return dest;
}

void
strmap_add(struct strmap *map, const char *key, const char *value)
{
    union {
        const char *in;
        void *out;
    } u = { .in = value };

    hashmap_add(map->hashmap, key, u.out);
}

const char *
strmap_set(struct strmap *map, const char *key, const char *value)
{
    union {
        const char *in;
        void *out;
    } u = { .in = value };

    return (const char*)hashmap_set(map->hashmap, key, u.out);
}

const char *
strmap_remove(struct strmap *map, const char *key)
{
    return (const char*)hashmap_remove(map->hashmap, key);
}

const char *
strmap_get(const struct strmap *map, const char *key)
{
    return (const char*)hashmap_get(map->hashmap, key);
}

const struct strmap_pair *
strmap_lookup_first(const struct strmap *map, const char *key)
{
    return (const struct strmap_pair *)hashmap_lookup_first(map->hashmap, key);
}

const struct strmap_pair *
strmap_lookup_next(const struct strmap_pair *pair)
{
    return (const struct strmap_pair *)
        hashmap_lookup_next((const struct hashmap_pair *)pair);
}

void
strmap_rewind(struct strmap *map)
{
    assert(map != nullptr);

    hashmap_rewind(map->hashmap);
}

const struct strmap_pair *
strmap_next(struct strmap *map)
{
    assert(map != nullptr);

    return (const struct strmap_pair*)hashmap_next(map->hashmap);
}
