/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "RootPool.hxx"
#include "pool.hxx"
#include "tpool.hxx"

#include <assert.h>

RootPool::RootPool()
    :p(*pool_new_libc(nullptr, "root")) {
    tpool_init(&p);
}

RootPool::~RootPool()
{
    gcc_unused auto ref = pool_unref(&p);
    assert(ref == 0);

    tpool_deinit();
    pool_commit();
    pool_recycler_clear();
}
