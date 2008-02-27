/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRMAP_H
#define __BENG_STRMAP_H

#include "pool.h"

typedef struct strmap *strmap_t;

struct strmap_pair {
    const char *key, *value;
};

strmap_t __attr_malloc
strmap_new(pool_t pool, unsigned capacity);

struct strmap *__attr_malloc
strmap_dup(pool_t pool, struct strmap *src);

void
strmap_addn(strmap_t map, const char *key, const char *value);

const char *
strmap_put(strmap_t map, const char *key, const char *value, int overwrite);

const char *
strmap_remove(strmap_t map, const char *key);

const char *
strmap_get(strmap_t map, const char *key);

void
strmap_rewind(strmap_t map);

const struct strmap_pair *
strmap_next(strmap_t map);

#endif
