/*
 * Hash map with string keys.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HASHMAP_H
#define __BENG_HASHMAP_H

#include "pool.h"

typedef struct hashmap *hashmap_t;

struct hashmap_pair {
    const char *key;
    void *value;
};

hashmap_t attr_malloc
hashmap_new(pool_t pool, unsigned capacity);

void
hashmap_addn(hashmap_t map, const char *key, void *value);

const char *
hashmap_put(hashmap_t map, const char *key, void *value, int overwrite);

const char *
hashmap_remove(hashmap_t map, const char *key);

const char *
hashmap_get(hashmap_t map, const char *key);

void
hashmap_rewind(hashmap_t map);

const struct hashmap_pair *
hashmap_next(hashmap_t map);

#endif
