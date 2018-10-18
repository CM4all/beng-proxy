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

#include "SlicePool.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>

static void
Fill(void *_p, size_t length, unsigned seed)
{
    for (uint8_t *p = (uint8_t *)_p, *end = p + length; p != end; ++p)
        *p = (uint8_t)seed++;
}

gcc_pure
static bool
Check(const void *_p, size_t length, unsigned seed)
{
    for (const uint8_t *p = (const uint8_t *)_p, *end = p + length;
         p != end; ++p)
        if (*p != (uint8_t)seed++)
            return false;

    return true;
}

TEST(SliceTest, Small)
{
    const size_t slice_size = 13;
    const unsigned per_area = 600;

    SlicePool pool(slice_size, per_area);

    auto allocation0 = pool.Alloc();
    auto *area0 = allocation0.area;
    ASSERT_NE(area0, nullptr);
    allocation0.Free();

    SliceAllocation allocations[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto &allocation = allocations[i];
        allocation = pool.Alloc();
        ASSERT_EQ(allocation.area, area0);
        ASSERT_NE(allocation.data, nullptr);
        ASSERT_TRUE(i <= 0 || allocation.data != allocations[0].data);
        ASSERT_TRUE(i <= 1 || allocation.data != allocations[1].data);
        ASSERT_TRUE(i <= 128 || allocation.data != allocations[128].data);

        Fill(allocation.data, slice_size, i);
    }

    SliceAllocation more[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        more[i] = pool.Alloc();

        ASSERT_TRUE(more[i].IsDefined());

        Fill(more[i].data, slice_size, per_area + i);
    }

    ASSERT_NE(more[per_area - 1].area, area0);

    for (unsigned i = 0; i < per_area; ++i) {
        ASSERT_TRUE(Check(allocations[i].data, slice_size, i));
        allocations[i].Free();

        ASSERT_TRUE(Check(more[i].data, slice_size, per_area + i));
        more[i].Free();
    }
}

TEST(SliceTest, Medium)
{
    const size_t slice_size = 3000;
    const unsigned per_area = 10;

    SlicePool pool(slice_size, per_area);

    auto allocation0 = pool.Alloc();
    auto *area0 = allocation0.area;
    ASSERT_NE(area0, nullptr);
    allocation0.Free();

    SliceAllocation allocations[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto &allocation = allocations[i];
        allocation = pool.Alloc();
        ASSERT_EQ(allocation.area, area0);

        ASSERT_NE(allocations[i].data, nullptr);
        ASSERT_TRUE(i <= 0 || allocation.data != allocations[0].data);
        ASSERT_TRUE(i <= 1 || allocation.data != allocations[1].data);
        ASSERT_TRUE(i <= per_area - 1 ||
                    allocation.data != allocations[per_area - 1].data);

        Fill(allocation.data, slice_size, i);
    }

    SliceAllocation more[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        more[i] = pool.Alloc();

        ASSERT_TRUE(more[i].IsDefined());

        Fill(more[i].data, slice_size, per_area + i);
    }

    for (unsigned i = 0; i < per_area; ++i) {
        ASSERT_TRUE(Check(allocations[i].data, slice_size, i));
        allocations[i].Free();

        ASSERT_TRUE(Check(more[i].data, slice_size, per_area + i));
        more[i].Free();
    }
}

TEST(SliceTest, Large)
{
    const size_t slice_size = 8192;
    const unsigned per_area = 13;

    SlicePool pool(slice_size, per_area);

    auto allocation0 = pool.Alloc();
    auto *area0 = allocation0.area;
    ASSERT_NE(area0, nullptr);
    allocation0.Free();

    SliceAllocation allocations[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto &allocation = allocations[i];
        allocation = pool.Alloc();
        ASSERT_EQ(allocation.area, area0);

        ASSERT_NE(allocations[i].data, nullptr);
        ASSERT_TRUE(i <= 0 || allocation.data != allocations[0].data);
        ASSERT_TRUE(i <= 1 || allocation.data != allocations[1].data);
        ASSERT_TRUE(i <= per_area - 1 ||
                    allocation.data != allocations[per_area - 1].data);

        Fill(allocation.data, slice_size, i);
    }

    SliceAllocation more[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        more[i] = pool.Alloc();

        ASSERT_TRUE(more[i].IsDefined());

        Fill(more[i].data, slice_size, per_area + i);
    }

    for (unsigned i = 0; i < per_area; ++i) {
        ASSERT_TRUE(Check(allocations[i].data, slice_size, i));
        allocations[i].Free();

        ASSERT_TRUE(Check(more[i].data, slice_size, per_area + i));
        more[i].Free();
    }
}
