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

#include "rubber.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <assert.h>
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

static void
FillRubber(Rubber &r, unsigned id, size_t length)
{
	Fill(r.Write(id), length, id);
}

static unsigned
AddFillRubber(Rubber &r, size_t length)
{
	unsigned id = r.Add(length);
	if (id != 0)
		FillRubber(r, id, length);

	return id;
}

gcc_pure
static bool
CheckRubber(Rubber &r, unsigned id, size_t length)
{
	return Check(r.Read(id), length, id);
}

TEST(RubberTest, Basic)
{
	size_t total = 4 * 1024 * 1024;

	Rubber r(total);

	total = r.GetMaxSize();

	/* fill the whole "rubber" object with four quarters */

	unsigned a = AddFillRubber(r, total / 4);
	ASSERT_GT(a, 0u);
	ASSERT_EQ(r.GetSizeOf(a), total / 4);

	unsigned b = AddFillRubber(r, total / 4);
	ASSERT_GT(b, 0u);
	ASSERT_EQ(r.GetSizeOf(b), total / 4);

	ASSERT_EQ(r.GetNettoSize(), total / 2);
	ASSERT_EQ(r.GetBruttoSize(), total / 2);

	unsigned c = AddFillRubber(r, total / 4);
	ASSERT_GT(c, 0u);
	ASSERT_EQ(r.GetSizeOf(c), total / 4);

	unsigned d = AddFillRubber(r, total / 4);
	ASSERT_GT(d, 0u);
	ASSERT_EQ(r.GetSizeOf(d), total / 4);

	ASSERT_EQ(r.GetNettoSize(), total);
	ASSERT_EQ(r.GetBruttoSize(), total);

	/* another allocation must fail */

	ASSERT_EQ(AddFillRubber(r, 1), 0u);

	ASSERT_TRUE(CheckRubber(r, a, total / 4));
	ASSERT_TRUE(CheckRubber(r, b, total / 4));
	ASSERT_TRUE(CheckRubber(r, c, total / 4));
	ASSERT_TRUE(CheckRubber(r, d, total / 4));

	/* remove two non-adjacent allocations; the following
	   rubber_add() call must automatically compress the "rubber"
	   object, and the allocation succeeds */

	r.Remove(b);
	r.Remove(d);

	ASSERT_EQ(r.GetNettoSize(), total / 2);
	ASSERT_EQ(r.GetBruttoSize(), total * 3 / 4);

	unsigned e = AddFillRubber(r, total / 2);
	ASSERT_GT(e, 0u);

	ASSERT_EQ(r.GetNettoSize(), total);
	ASSERT_EQ(r.GetBruttoSize(), total);

	ASSERT_TRUE(CheckRubber(r, a, total / 4));
	ASSERT_TRUE(CheckRubber(r, c, total / 4));
	ASSERT_TRUE(CheckRubber(r, e, total / 2));

	/* remove one after another, and see if rubber results are
	   correct */

	r.Remove(a);

	ASSERT_EQ(r.GetNettoSize(), total * 3 / 4);
	ASSERT_EQ(r.GetBruttoSize(), total);

	r.Compress();

	ASSERT_EQ(r.GetNettoSize(), total * 3 / 4);
	ASSERT_EQ(r.GetBruttoSize(), total * 3 / 4);
	ASSERT_TRUE(CheckRubber(r, c, total / 4));
	ASSERT_TRUE(CheckRubber(r, e, total / 2));

	r.Remove(c);

	ASSERT_EQ(r.GetNettoSize(), total / 2);
	ASSERT_EQ(r.GetBruttoSize(), total * 3 / 4);
	ASSERT_TRUE(CheckRubber(r, e, total / 2));

	r.Compress();

	ASSERT_EQ(r.GetNettoSize(), total / 2);
	ASSERT_EQ(r.GetBruttoSize(), total / 2);
	ASSERT_TRUE(CheckRubber(r, e, total / 2));

	r.Remove(e);

	ASSERT_EQ(r.GetNettoSize(), size_t(0u));
	ASSERT_EQ(r.GetBruttoSize(), size_t(0u));

	r.Compress();

	ASSERT_EQ(r.GetNettoSize(), size_t(0u));
	ASSERT_EQ(r.GetBruttoSize(), size_t(0u));
}

TEST(RubberTest, Shrink)
{
	size_t total = 4 * 1024 * 1024;

	Rubber r(total);

	total = r.GetMaxSize();

	/* fill the whole "rubber" object */

	unsigned a = AddFillRubber(r, total * 3 / 4);
	ASSERT_GT(a, 0u);
	ASSERT_EQ(r.GetSizeOf(a), total * 3 / 4);

	unsigned b = AddFillRubber(r, total / 4);
	ASSERT_GT(b, 0u);

	ASSERT_EQ(r.GetNettoSize(), total);
	ASSERT_EQ(r.GetBruttoSize(), total);

	/* another allocation must fail */

	ASSERT_EQ(AddFillRubber(r, 1), 0u);

	ASSERT_TRUE(CheckRubber(r, a, total * 3 / 4));
	ASSERT_TRUE(CheckRubber(r, b, total / 4));

	/* shrink the first allocation, try again */

	r.Shrink(a, total / 4);
	ASSERT_EQ(r.GetSizeOf(a), total / 4);

	ASSERT_EQ(r.GetNettoSize(), total / 2);
	ASSERT_EQ(r.GetBruttoSize(), total);

	unsigned c = AddFillRubber(r, total / 2);
	ASSERT_GT(c, 0u);

	ASSERT_EQ(r.GetNettoSize(), total);
	ASSERT_EQ(r.GetBruttoSize(), total);

	ASSERT_TRUE(CheckRubber(r, a, total / 4));
	ASSERT_TRUE(CheckRubber(r, b, total / 4));
	ASSERT_TRUE(CheckRubber(r, c, total / 2));

	/* shrink the third allocation, verify rubber_compress() */

	r.Shrink(c, total / 4);

	ASSERT_EQ(r.GetNettoSize(), total * 3 / 4);
	ASSERT_EQ(r.GetBruttoSize(), total);

	ASSERT_TRUE(CheckRubber(r, a, total / 4));
	ASSERT_TRUE(CheckRubber(r, b, total / 4));
	ASSERT_TRUE(CheckRubber(r, c, total / 4));

	r.Compress();

	ASSERT_EQ(r.GetNettoSize(), total * 3 / 4);
	ASSERT_EQ(r.GetBruttoSize(), total * 3 / 4);

	ASSERT_TRUE(CheckRubber(r, a, total / 4));
	ASSERT_TRUE(CheckRubber(r, b, total / 4));
	ASSERT_TRUE(CheckRubber(r, c, total / 4));

	/* clean up */

	r.Remove(a);
	r.Remove(b);
	r.Remove(c);
}

/**
 * Fill the allocation table, see if the allocator fails
 * eventually even though there's memory available.
 */
TEST(RubberTest, FullTable)
{
	size_t total = 64 * 1024 * 1024;

	Rubber r(total);

	total = r.GetMaxSize();

	static const size_t max = 300000;
	static unsigned ids[max], n = 0;
	while (n < max) {
		unsigned id = r.Add(1);
		if (id == 0)
			break;

		ASSERT_EQ((size_t)r.Read(id) % 0x10, size_t(0));

		ids[n++] = id;
	}

	ASSERT_GT(n, 0u);
	ASSERT_LT(n, max);

	/* just to be sure: try again, must still fail */

	ASSERT_EQ(r.Add(1024 * 1024), 0u);

	/* remove one item; now a large allocation must succeed */

	r.Remove(ids[n / 2]);

	unsigned id = r.Add(1024 * 1024);
	ASSERT_GT(id, 0u);
	ASSERT_EQ(id, ids[n / 2]);

	/* cleanup */

	for (unsigned i = 0; i < n; ++i)
		r.Remove(ids[i]);
}
