/*
 * Implementation of D. J. Bernstein's cdb hash function.
 * http://cr.yp.to/cdb/cdb.txt
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "djbhash.h"

#include <assert.h>

gcc_const
static uint_fast32_t
djb_hash_update(uint_fast32_t hash, uint8_t b)
{
    return (hash * 33) ^ b;
}

uint32_t
djb_hash(const void *p0, size_t size)
{
    assert(p0 != NULL);

    uint_fast32_t hash = 5381;

    const uint8_t *p = p0;
    for (size_t i = 0; i != size; ++i)
        hash = djb_hash_update(hash, p[i]);

    return hash;
}

uint32_t
djb_hash_string(const char *p0)
{
    assert(p0 != NULL);

    uint_fast32_t hash = 5381;

    for (const uint8_t *p = (const uint8_t *)p0; *p != 0; ++p)
        hash = djb_hash_update(hash, *p);

    return hash;
}
