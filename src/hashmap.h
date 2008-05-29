/*
 * Hash map with string keys.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HASHMAP_H
#define __BENG_HASHMAP_H

#include "pool.h"

struct hashmap_pair {
    const char *key;
    void *value;
};

struct hashmap *__attr_malloc
hashmap_new(pool_t pool, unsigned capacity);

void
hashmap_add(struct hashmap *map, const char *key, void *value);

void *
hashmap_set(struct hashmap *map, const char *key, void *value);

void *
hashmap_remove(struct hashmap *map, const char *key);

void *
hashmap_get(struct hashmap *map, const char *key);

void
hashmap_rewind(struct hashmap *map);

const struct hashmap_pair *
hashmap_next(struct hashmap *map);

#endif
