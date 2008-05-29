/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.h"
#include "hashmap.h"

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
strmap_new(pool_t pool, unsigned capacity)
{
    struct strmap *map = p_calloc(pool, sizeof(*map));
    assert(capacity > 1);

    map->capacity = capacity;
    map->hashmap = hashmap_new(pool, capacity);

    return map;
}

struct strmap *__attr_malloc
strmap_dup(pool_t pool, struct strmap *src)
{
    struct strmap *dest = strmap_new(pool, src->capacity);
    const struct strmap_pair *pair;

    strmap_rewind(src);
    while ((pair = strmap_next(src)) != NULL)
        strmap_addn(dest, p_strdup(pool, pair->key),
                    p_strdup(pool, pair->value));

    return dest;
}

void
strmap_addn(struct strmap *map, const char *key, const char *value)
{
    union {
        const char *in;
        void *out;
    } u = { .in = value };

    hashmap_addn(map->hashmap, key, u.out);
}

const char *
strmap_put(struct strmap *map, const char *key, const char *value,
           bool overwrite)
{
    union {
        const char *in;
        void *out;
    } u = { .in = value };

    return (const char*)hashmap_put(map->hashmap, key, u.out, overwrite);
}

const char *
strmap_remove(struct strmap *map, const char *key)
{
    return (const char*)hashmap_remove(map->hashmap, key);
}

const char *
strmap_get(struct strmap *map, const char *key)
{
    return (const char*)hashmap_get(map->hashmap, key);
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
