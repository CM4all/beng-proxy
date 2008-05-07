/*
 * String allocation for distributed pools.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "dpool.h"

#include <assert.h>
#include <string.h>

char *
d_memdup(struct dpool *pool, const void *src, size_t length)
{
    void *dest = d_malloc(pool, length);
    if (dest == NULL)
        return NULL;

    memcpy(dest, src, length);
    return dest;
}

char *
d_strdup(struct dpool *pool, const char *src)
{
    return d_memdup(pool, src, strlen(src) + 1);
}

char *
d_strndup(struct dpool *pool, const char *src, size_t length)
{
    char *dest = d_malloc(pool, length + 1);
    if (dest == NULL)
        return NULL;

    memcpy(dest, src, length);
    dest[length] = 0;
    return dest;
}
