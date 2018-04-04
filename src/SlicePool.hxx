/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The "slice" memory allocator.  It is an allocator for large numbers
 * of small fixed-size objects.
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
slice_pool_new(size_t slice_size, unsigned per_area) noexcept;

gcc_nonnull_all
void
slice_pool_free(SlicePool *pool) noexcept;

/**
 * Controls whether forked child processes inherit the allocator.
 * This is enabled by default.
 */
void
slice_pool_fork_cow(SlicePool &pool, bool inherit) noexcept;

gcc_const gcc_nonnull_all
size_t
slice_pool_get_slice_size(const SlicePool *pool) noexcept;

gcc_nonnull_all
void
slice_pool_compress(SlicePool *pool) noexcept;

gcc_nonnull_all
SliceAllocation
slice_alloc(SlicePool *pool) noexcept;

gcc_nonnull_all
void
slice_free(SlicePool *pool, SliceArea *area, void *p) noexcept;

gcc_pure
AllocatorStats
slice_pool_get_stats(const SlicePool &pool) noexcept;

#endif
