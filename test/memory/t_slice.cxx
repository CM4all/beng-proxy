// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "memory/Checker.hxx"
#include "memory/SlicePool.hxx"
#include "util/Sanitizer.hxx"

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>

static void
Fill(void *_p, size_t length, unsigned seed)
{
	for (uint8_t *p = (uint8_t *)_p, *end = p + length; p != end; ++p)
		*p = (uint8_t)seed++;
}

[[gnu::pure]]
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

	SlicePool pool{slice_size, per_area, "slice"};

	auto allocation0 = pool.Alloc();
	auto *area0 = allocation0.area;
	if (!HaveMemoryChecker()) {
		ASSERT_NE(area0, nullptr);
	}
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

	if (!HaveMemoryChecker()) {
		ASSERT_NE(more[per_area - 1].area, area0);
	}

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

	SlicePool pool{slice_size, per_area, "slice"};

	auto allocation0 = pool.Alloc();
	auto *area0 = allocation0.area;
	if (!HaveMemoryChecker()) {
		ASSERT_NE(area0, nullptr);
	}
	allocation0.Free();

	SliceAllocation allocations[per_area];

	for (unsigned i = 0; i < per_area; ++i) {
		auto &allocation = allocations[i];
		allocation = pool.Alloc();

		if (!HaveMemoryChecker()) {
			ASSERT_EQ(allocation.area, area0);
		}

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

	SlicePool pool{slice_size, per_area, "slice"};

	auto allocation0 = pool.Alloc();
	auto *area0 = allocation0.area;
	if (!HaveMemoryChecker()) {
		ASSERT_NE(area0, nullptr);
	}
	allocation0.Free();

	SliceAllocation allocations[per_area];

	for (unsigned i = 0; i < per_area; ++i) {
		auto &allocation = allocations[i];
		allocation = pool.Alloc();

		if (!HaveMemoryChecker()) {
			ASSERT_EQ(allocation.area, area0);
		}

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
