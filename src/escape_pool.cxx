/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_pool.hxx"
#include "escape_class.hxx"
#include "pool.h"

#include <assert.h>

char *
escape_dup(struct pool *pool, const struct escape_class *cls,
           const char *p, size_t length)
{
    assert(cls != nullptr);
    assert(cls->escape_size != nullptr);
    assert(cls->escape != nullptr);
    assert(p != nullptr);

    size_t size = cls->escape_size(p, length);
    if (size == 0)
        return p_strndup(pool, p, length);

    char *q = (char *)p_malloc(pool, size + 1);
    size_t out_size = cls->escape(p, length, q);
    assert(out_size <= size);
    q[out_size] = 0;

    return q;
}

