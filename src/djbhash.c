/*
 * Implementation of D. J. Bernstein's cdb hash function.
 * http://cr.yp.to/cdb/cdb.txt
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "djbhash.h"

#include <assert.h>
#include <stdint.h>

unsigned
djb_hash(const void *p0, size_t size)
{
    assert(p0 != NULL);

    unsigned hash = 5381;

    const uint8_t *p = p0;
    for (size_t i = 0; i != size; ++i)
        hash = (hash << 5) + hash + p[i];

    return hash;
}

unsigned
djb_hash_string(const char *p0)
{
    assert(p0 != NULL);

    unsigned hash = 5381;

    for (const uint8_t *p = (const uint8_t *)p0; *p != 0; ++p)
        hash = (hash << 5) + hash + *p;

    return hash;
}
