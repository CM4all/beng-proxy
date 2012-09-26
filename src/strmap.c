/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.h"
#include "hashmap.h"
#include "pool.h"

#include <assert.h>

struct slot {
    struct slot *next;
    struct strmap_pair pair;
};

struct strmap {
    unsigned capacity;

    struct hashmap *hashmap;
};

struct strmap *
strmap_new(struct pool *pool, unsigned capacity)
{
    struct strmap *map = p_calloc(pool, sizeof(*map));
    assert(capacity > 1);

    map->capacity = capacity;
    map->hashmap = hashmap_new(pool, capacity);

    return map;
}

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, struct strmap *src)
{
    struct strmap *dest = strmap_new(pool, src->capacity);
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != NULL)
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

const char *
strmap_get_next(const struct strmap *map, const char *key, const char *prev)
{
    return (const char*)hashmap_get_next(map->hashmap, key, prev);
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
    assert(map != NULL);

    hashmap_rewind(map->hashmap);
}

const struct strmap_pair *
strmap_next(struct strmap *map)
{
    assert(map != NULL);

    return (const struct strmap_pair*)hashmap_next(map->hashmap);
}
