/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRMAP_H
#define __BENG_STRMAP_H

#include <inline/compiler.h>

#include <stddef.h>

struct pool;

struct strmap_pair {
    const char *key, *value;
};

struct strmap *gcc_malloc
strmap_new(struct pool *pool, unsigned capacity);

struct strmap *gcc_malloc
strmap_dup(struct pool *pool, struct strmap *src, unsigned capacity);

void
strmap_add(struct strmap *map, const char *key, const char *value);

const char *
strmap_set(struct strmap *map, const char *key, const char *value);

const char *
strmap_remove(struct strmap *map, const char *key);

gcc_pure
const char *
strmap_get(const struct strmap *map, const char *key);

gcc_pure
const char *
strmap_get_next(const struct strmap *map, const char *key, const char *prev);

void
strmap_rewind(struct strmap *map);

const struct strmap_pair *
strmap_next(struct strmap *map);

/**
 * This variation of strmap_remove() allows the caller to pass map=NULL.
 */
static inline const char *
strmap_remove_checked(struct strmap *map, const char *key)
{
    return map != NULL
        ? strmap_remove(map, key)
        : NULL;
}

/**
 * This variation of strmap_get() allows the caller to pass map=NULL.
 */
gcc_pure
static inline const char *
strmap_get_checked(const struct strmap *map, const char *key)
{
    return map != NULL
        ? strmap_get(map, key)
        : NULL;
}

#endif
