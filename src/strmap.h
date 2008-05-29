/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRMAP_H
#define __BENG_STRMAP_H

#include "pool.h"

struct strmap_pair {
    const char *key, *value;
};

struct strmap *__attr_malloc
strmap_new(pool_t pool, unsigned capacity);

struct strmap *__attr_malloc
strmap_dup(pool_t pool, struct strmap *src);

void
strmap_add(struct strmap *map, const char *key, const char *value);

const char *
strmap_set(struct strmap *map, const char *key, const char *value);

const char *
strmap_remove(struct strmap *map, const char *key);

const char *
strmap_get(struct strmap *map, const char *key);

void
strmap_rewind(struct strmap *map);

const struct strmap_pair *
strmap_next(struct strmap *map);

#endif
