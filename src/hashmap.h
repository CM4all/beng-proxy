/*
 * Hash map with string keys.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HASHMAP_H
#define __BENG_HASHMAP_H

#include <inline/compiler.h>

#include <stdbool.h>

struct pool;

struct hashmap_pair {
    const char *key;
    void *value;
};

struct hashmap *gcc_malloc
hashmap_new(struct pool *pool, unsigned capacity);

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
 * Removes all matching pair.s
 */
void
hashmap_remove_match(struct hashmap *map, const char *key,
                     bool (*match)(void *value, void *ctx), void *ctx);

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

gcc_pure
void *
hashmap_get(const struct hashmap *map, const char *key);

/**
 * Returns another value for this key.
 *
 * @param prev the previous value returned by hashmap_get() or this
 * function
 * @return the next value, or NULL if there are no more
 */
gcc_pure
void *
hashmap_get_next(const struct hashmap *map, const char *key, const void *prev);

/**
 * Find the first value for this key.  The return value can be used
 * with hashmap_lookup_next() to iterate over all values for a certain
 * key efficiently.
 */
gcc_pure
const struct hashmap_pair *
hashmap_lookup_first(const struct hashmap *map, const char *key);

/**
 * Returns another value for the same key.
 *
 * @param prev the previous value returned by hashmap_lookup_first()
 * or this function
 * @return the next value, or NULL if there are no more
 */
gcc_pure
const struct hashmap_pair *
hashmap_lookup_next(const struct hashmap_pair *prev);

void
hashmap_rewind(struct hashmap *map);

const struct hashmap_pair *
hashmap_next(struct hashmap *map);

#endif
