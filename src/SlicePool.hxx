/*
 * The "slice" memory allocator.  It is an allocator for large numbers
 * of small fixed-size objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SLICE_POOL_HXX
#define BENG_PROXY_SLICE_POOL_HXX

#include "util/Compiler.h"

#include <stddef.h>

struct AllocatorStats;
struct SlicePool;
struct SliceArea;

struct SliceAllocation {
    SliceArea *area;

    void *data;
    size_t size;
};

SlicePool *
slice_pool_new(size_t slice_size, unsigned per_area);

gcc_nonnull_all
void
slice_pool_free(SlicePool *pool);

/**
 * Controls whether forked child processes inherit the allocator.
 * This is enabled by default.
 */
void
slice_pool_fork_cow(SlicePool &pool, bool inherit);

gcc_const gcc_nonnull_all
size_t
slice_pool_get_slice_size(const SlicePool *pool);

gcc_nonnull_all
void
slice_pool_compress(SlicePool *pool);

gcc_nonnull_all
SliceAllocation
slice_alloc(SlicePool *pool);

gcc_nonnull_all
void
slice_free(SlicePool *pool, SliceArea *area, void *p);

gcc_pure
AllocatorStats
slice_pool_get_stats(const SlicePool &pool);

#endif
