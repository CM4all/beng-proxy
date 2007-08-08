/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRMAP_H
#define __BENG_STRMAP_H

#include "pool.h"

typedef struct strmap *strmap_t;

strmap_t
strmap_new(pool_t pool, unsigned capacity);

void
strmap_addn(strmap_t map, const char *key, const char *value);

const char *
strmap_get(strmap_t map, const char *key);

#endif
