/*
 * Allocating struct ConstBuffer from memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pbuffer.hxx"

#include <assert.h>
#include <string.h>

ConstBuffer<void>
LazyCatBuffer(struct pool &pool, ConstBuffer<void> a, ConstBuffer<void> b)
{
    assert(!a.IsNull());
    assert(!b.IsNull());

    if (a.size == 0)
        /* no need to allocate a new buffer */
        return b;

    if (b.size == 0)
        /* no need to allocate a new buffer */
        return a;

    size_t size = a.size + b.size;
    void *result = p_malloc(&pool, size);
    mempcpy(mempcpy(result, a.data, a.size), b.data, b.size);
    return { result, size };
}
