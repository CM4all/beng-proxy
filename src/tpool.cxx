/*
 * Temporary memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tpool.hxx"

#include <assert.h>

struct pool *tpool;

void
tpool_init(struct pool *parent)
{
    assert(tpool == nullptr);

    tpool = pool_new_linear(parent, "temporary_pool", 32768);
}

void
tpool_deinit()
{
    gcc_unused unsigned ref;
    ref = pool_unref(tpool);
    assert(ref == 0);
}
