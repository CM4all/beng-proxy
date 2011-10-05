/*
 * Hash map with string keys, stored in mmap (distributed over several
 * worker processes).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HASHMAP_H
#define __BENG_HASHMAP_H

#include <inline/compiler.h>

struct dpool;

struct dhashmap_pair {
    const char *key;
    void *value;
};

struct dhashmap *gcc_malloc
dhashmap_new(struct dpool *pool, unsigned capacity);

void
dhashmap_free(struct dhashmap *map);

void *
dhashmap_put(struct dhashmap *map, const char *key, void *value);

void *
dhashmap_remove(struct dhashmap *map, const char *key);

void *
dhashmap_get(struct dhashmap *map, const char *key);

void
dhashmap_rewind(struct dhashmap *map);

const struct dhashmap_pair *
dhashmap_next(struct dhashmap *map);

#endif
