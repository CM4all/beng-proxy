/*
 * Temporary memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tpool.h"

#include <assert.h>

pool_t tpool;

void
tpool_init(pool_t parent)
{
    assert(tpool == NULL);

    tpool = pool_new_linear(parent, "temporary_pool", 8192);
}

void
tpool_deinit(void)
{
    unsigned ref;
    ref = pool_unref(tpool);
    assert(ref == 0);
}
