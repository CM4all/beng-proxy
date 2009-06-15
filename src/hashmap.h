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

/**
 * Removes the item with the specified value.  Returns false if no
 * such value was found.
 */
bool
hashmap_remove_value(struct hashmap *map, const char *key, const void *value);

/**
 * Iterates through the hashmap, invokes the match() callback
 * function, and removes every "matching" value.
 *
 * @return the number of items which were removed
 */
unsigned
hashmap_remove_all_match(struct hashmap *map,
                         bool (*match)(const char *key, void *value,
                                       void *ctx),
                         void *ctx);

void *
hashmap_get(const struct hashmap *map, const char *key);

/**
 * Returns another value for this key.
 *
 * @param prev the previous value returned by hashmap_get() or this
 * function
 * @return the next value, or NULL if there are no more
 */
void *
hashmap_get_next(const struct hashmap *map, const char *key, const void *prev);

void
hashmap_rewind(struct hashmap *map);

const struct hashmap_pair *
hashmap_next(struct hashmap *map);

#endif
