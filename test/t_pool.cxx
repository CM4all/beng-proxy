/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/RootPool.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>

TEST(PoolTest, Libc)
{
	RootPool pool;
	ASSERT_EQ(size_t(0), pool_brutto_size(pool));
	ASSERT_EQ(size_t(0), pool_netto_size(pool));

	const void *q = p_malloc(pool, 64);
	ASSERT_NE(q, nullptr);
	ASSERT_EQ(size_t(64), pool_brutto_size(pool));
	ASSERT_EQ(size_t(64), pool_netto_size(pool));

	const void *r = p_malloc(pool, 256);
	ASSERT_NE(r, nullptr);
	ASSERT_EQ(size_t(256 + 64), pool_brutto_size(pool));
	ASSERT_EQ(size_t(256 + 64), pool_netto_size(pool));

	p_free(pool, q, 64);
	ASSERT_EQ(size_t(256), pool_brutto_size(pool));
	ASSERT_EQ(size_t(256), pool_netto_size(pool));
	p_free(pool, r, 256);
	ASSERT_EQ(size_t(0), pool_brutto_size(pool));
	ASSERT_EQ(size_t(0), pool_netto_size(pool));
}

TEST(PoolTest, Linear)
{
	RootPool root_pool;
	const auto pool = pool_new_linear(root_pool, "foo", 64);
#ifdef NDEBUG
	ASSERT_EQ(size_t(0), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(0), pool_netto_size(pool));

	const void *q = p_malloc(pool, 1024);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024), pool_netto_size(pool));

	q = p_malloc(pool, 32);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024 + 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024 + 32), pool_netto_size(pool));

	q = p_malloc(pool, 16);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024 + 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024 + 32 + 16), pool_netto_size(pool));

	q = p_malloc(pool, 32);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(1024 + 2 * 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(1024 + 32 + 16 + 32), pool_netto_size(pool));

	q = p_malloc(pool, 1024);
	ASSERT_NE(q, nullptr);
#ifdef NDEBUG
	ASSERT_EQ(size_t(2 * 1024 + 2 * 64), pool_brutto_size(pool));
#endif
	ASSERT_EQ(size_t(2 * 1024 + 32 + 16 + 32), pool_netto_size(pool));
}
