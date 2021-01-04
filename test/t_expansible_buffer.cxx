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

#include "expansible_buffer.hxx"
#include "TestPool.hxx"
#include "util/ConstBuffer.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(ExpansibleBufferTest, Basic)
{
	TestPool pool;

	ExpansibleBuffer eb(pool, 4, 1024);
	ASSERT_TRUE(eb.IsEmpty());

	auto p = eb.Read();
	ASSERT_NE(p.data, nullptr);
	ASSERT_EQ(p.size, 0u);

	eb.Write("01");
	ASSERT_FALSE(eb.IsEmpty());

	auto q = eb.Read();
	ASSERT_EQ(q.data, p.data);
	ASSERT_EQ(q.size, 2u);
	ASSERT_EQ(memcmp(q.data, "01", 2), 0);

	eb.Write("234");
	ASSERT_FALSE(eb.IsEmpty());

	q = eb.Read();
	ASSERT_NE(q.data, p.data);
	ASSERT_EQ(q.size, 5u);
	ASSERT_EQ(memcmp(q.data, "01234", 5), 0);

	eb.Clear();
	ASSERT_TRUE(eb.IsEmpty());

	p = eb.Read();
	ASSERT_EQ(p.data, q.data);
	ASSERT_EQ(p.size, 0u);

	eb.Write("abcdef");
	ASSERT_FALSE(eb.IsEmpty());

	p = eb.Read();
	ASSERT_EQ(p.data, q.data);
	ASSERT_EQ(p.size, 6u);
	ASSERT_EQ(memcmp(q.data, "abcdef", 6), 0);

	void *r = eb.Write(512);
	ASSERT_NE(r, nullptr);

	/* this call hits the hard limit */
	r = eb.Write(512);
	ASSERT_EQ(r, nullptr);
}
