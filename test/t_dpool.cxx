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

#include "shm/shm.hxx"
#include "shm/dpool.hxx"

#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>

TEST(ShmTest, Dpool)
{
    void *a, *b, *c, *d;

    auto *shm = shm_new(1024, 2);
    ASSERT_NE(shm, nullptr);

    auto *pool = dpool_new(*shm);
    ASSERT_NE(pool, nullptr);

    a = shm_alloc(shm, 1);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(a, pool);

    b = shm_alloc(shm, 1);
    ASSERT_EQ(b, nullptr);

    shm_free(shm, a);

    a = d_malloc(*pool, 512);
    ASSERT_NE(a, nullptr);
    memset(a, 0, 512);

    b = d_malloc(*pool, 800);
    ASSERT_NE(b, nullptr);
    memset(b, 0, 800);

    try {
        c = d_malloc(*pool, 512);
        ASSERT_EQ(c, nullptr);
    } catch (const std::bad_alloc &) {
    }

    d = d_malloc(*pool, 220);
    ASSERT_NE(d, nullptr);

    d_free(*pool, a);

    a = d_malloc(*pool, 240);
    ASSERT_NE(a, nullptr);

    try {
        c = d_malloc(*pool, 270);
        ASSERT_EQ(c, nullptr);
    } catch (const std::bad_alloc &) {
    }

    /* no free SHM page */
    c = shm_alloc(shm, 1);
    ASSERT_EQ(c, nullptr);

    /* free "b" which should release one SHM page */
    d_free(*pool, b);

    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    dpool_destroy(pool);
    shm_close(shm);
}
