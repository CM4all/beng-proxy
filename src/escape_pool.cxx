/*
 * Escape classes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_pool.hxx"
#include "escape_class.hxx"
#include "pool.hxx"

#include <assert.h>

char *
escape_dup(struct pool *pool, const struct escape_class *cls,
           StringView p)
{
    assert(cls != nullptr);
    assert(cls->escape_size != nullptr);
    assert(cls->escape != nullptr);

    size_t size = cls->escape_size(p);
    if (size == 0)
        return p_strdup(*pool, p);

    char *q = (char *)p_malloc(pool, size + 1);
    size_t out_size = cls->escape(p, q);
    assert(out_size <= size);
    q[out_size] = 0;

    return q;
}

