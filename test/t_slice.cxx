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

    auto *pool = slice_pool_new(slice_size, per_area);
    ASSERT_NE(pool, nullptr);

    auto allocation0 = slice_alloc(pool);
    auto *area0 = allocation0.area;
    ASSERT_NE(area0, nullptr);
    slice_free(pool, area0, allocation0.data);

    void *allocations[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto allocation = slice_alloc(pool);
        ASSERT_EQ(allocation.area, area0);

        allocations[i] = allocation.data;
        ASSERT_NE(allocations[i], nullptr);
        ASSERT_TRUE(i <= 0 || allocations[i] != allocations[0]);
        ASSERT_TRUE(i <= 1 || allocations[i] != allocations[1]);
        ASSERT_TRUE(i <= 128 || allocations[i] != allocations[128]);

        Fill(allocations[i], slice_size, i);
    }

    struct {
        SliceArea *area;
        void *p;
    } more[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto allocation = slice_alloc(pool);

        more[i].area = allocation.area;
        more[i].p = allocation.data;
        ASSERT_NE(more[i].p, nullptr);

        Fill(more[i].p, slice_size, per_area + i);
    }

    ASSERT_NE(more[per_area - 1].area, area0);

    for (unsigned i = 0; i < per_area; ++i) {
        ASSERT_TRUE(Check(allocations[i], slice_size, i));
        slice_free(pool, area0, allocations[i]);

        ASSERT_TRUE(Check(more[i].p, slice_size, per_area + i));
        slice_free(pool, more[i].area, more[i].p);
    }

    slice_pool_free(pool);
}

TEST(SliceTest, Medium)
{
    const size_t slice_size = 3000;
    const unsigned per_area = 10;

    auto *pool = slice_pool_new(slice_size, per_area);
    ASSERT_NE(pool, nullptr);

    auto allocation0 = slice_alloc(pool);
    auto *area0 = allocation0.area;
    ASSERT_NE(area0, nullptr);
    slice_free(pool, area0, allocation0.data);

    void *allocations[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto allocation = slice_alloc(pool);
        ASSERT_EQ(allocation.area, area0);

        allocations[i] = allocation.data;
        ASSERT_NE(allocations[i], nullptr);
        ASSERT_TRUE(i <= 0 || allocations[i] != allocations[0]);
        ASSERT_TRUE(i <= 1 || allocations[i] != allocations[1]);
        ASSERT_TRUE(i <= per_area - 1 ||
                    allocations[i] != allocations[per_area - 1]);

        Fill(allocations[i], slice_size, i);
    }

    struct {
        SliceArea *area;
        void *p;
    } more[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto allocation = slice_alloc(pool);

        more[i].area = allocation.area;
        more[i].p = allocation.data;
        ASSERT_NE(more[i].p, nullptr);

        Fill(more[i].p, slice_size, per_area + i);
    }

    for (unsigned i = 0; i < per_area; ++i) {
        ASSERT_TRUE(Check(allocations[i], slice_size, i));
        slice_free(pool, area0, allocations[i]);

        ASSERT_TRUE(Check(more[i].p, slice_size, per_area + i));
        slice_free(pool, more[i].area, more[i].p);
    }

    slice_pool_free(pool);
}

TEST(SliceTest, Large)
{
    const size_t slice_size = 8192;
    const unsigned per_area = 13;

    auto *pool = slice_pool_new(slice_size, per_area);
    ASSERT_NE(pool, nullptr);

    auto allocation0 = slice_alloc(pool);
    auto *area0 = allocation0.area;
    ASSERT_NE(area0, nullptr);
    slice_free(pool, area0, allocation0.data);

    void *allocations[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto allocation = slice_alloc(pool);
        ASSERT_EQ(allocation.area, area0);

        allocations[i] = allocation.data;
        ASSERT_NE(allocations[i], nullptr);
        ASSERT_TRUE(i <= 0 || allocations[i] != allocations[0]);
        ASSERT_TRUE(i <= 1 || allocations[i] != allocations[1]);
        ASSERT_TRUE(i <= per_area - 1 ||
                    allocations[i] != allocations[per_area - 1]);

        Fill(allocations[i], slice_size, i);
    }

    struct {
        SliceArea *area;
        void *p;
    } more[per_area];

    for (unsigned i = 0; i < per_area; ++i) {
        auto allocation = slice_alloc(pool);

        more[i].area = allocation.area;
        more[i].p = allocation.data;
        ASSERT_NE(more[i].p, nullptr);

        Fill(more[i].p, slice_size, per_area + i);
    }

    for (unsigned i = 0; i < per_area; ++i) {
        ASSERT_TRUE(Check(allocations[i], slice_size, i));
        slice_free(pool, area0, allocations[i]);

        ASSERT_TRUE(Check(more[i].p, slice_size, per_area + i));
        slice_free(pool, more[i].area, more[i].p);
    }

    slice_pool_free(pool);
}
