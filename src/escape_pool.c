/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_pool.h"
#include "escape_class.h"
#include "pool.h"

#include <assert.h>

char *
escape_dup(struct pool *pool, const struct escape_class *class,
           const char *p, size_t length)
{
    assert(class != NULL);
    assert(class->escape_size != NULL);
    assert(class->escape != NULL);
    assert(p != NULL);

    size_t size = class->escape_size(p, length);
    if (size == 0)
        return p_strndup(pool, p, length);

    char *q = p_malloc(pool, size + 1);
    size_t out_size = class->escape(p, length, q);
    assert(out_size <= size);
    q[out_size] = 0;

    return q;
}

