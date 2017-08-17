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

#include <gtest/gtest.h>

TEST(ShmTest, Basic)
{
    struct shm *shm;
    void *a, *b, *c, *d, *e;

    shm = shm_new(1024, 2);

    a = shm_alloc(shm, 1);
    ASSERT_NE(a, nullptr);

    b = shm_alloc(shm, 2);
    ASSERT_EQ(b, nullptr);

    b = shm_alloc(shm, 1);
    ASSERT_NE(b, nullptr);

    c = shm_alloc(shm, 1);
    ASSERT_EQ(c, nullptr);

    shm_free(shm, a);
    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    a = shm_alloc(shm, 1);
    ASSERT_EQ(a, nullptr);

    shm_free(shm, b);
    shm_free(shm, c);

    a = shm_alloc(shm, 2);
    ASSERT_NE(a, nullptr);

    b = shm_alloc(shm, 2);
    ASSERT_EQ(b, nullptr);

    b = shm_alloc(shm, 1);
    ASSERT_EQ(b, nullptr);

    shm_free(shm, a);

    a = shm_alloc(shm, 2);
    ASSERT_NE(a, nullptr);

    shm_close(shm);

    /* allocate and deallocate in different order, to see if adjacent
       free pages are merged properly */

    shm = shm_new(1024, 5);

    a = shm_alloc(shm, 1);
    ASSERT_NE(a, nullptr);

    b = shm_alloc(shm, 2);
    ASSERT_NE(b, nullptr);

    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    d = shm_alloc(shm, 1);
    ASSERT_NE(d, nullptr);

    e = shm_alloc(shm, 1);
    ASSERT_EQ(e, nullptr);

    shm_free(shm, b);
    shm_free(shm, c);

    e = shm_alloc(shm, 4);
    ASSERT_EQ(e, nullptr);

    e = shm_alloc(shm, 3);
    ASSERT_NE(e, nullptr);
    shm_free(shm, e);

    b = shm_alloc(shm, 2);
    ASSERT_NE(b, nullptr);

    c = shm_alloc(shm, 1);
    ASSERT_NE(c, nullptr);

    shm_free(shm, c);
    shm_free(shm, b);

    e = shm_alloc(shm, 4);
    ASSERT_EQ(e, nullptr);

    e = shm_alloc(shm, 3);
    ASSERT_NE(e, nullptr);
    shm_free(shm, e);

    shm_close(shm);
}
