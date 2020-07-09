/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "TestPool.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "event/Loop.hxx"
#include "pool/pool.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/FailIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/Cancellable.hxx"

#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct Data final : RubberSinkHandler {
	TestPool pool;

	enum Result {
		NONE, DONE, OOM, TOO_LARGE, ERROR
	} result;

	Rubber &r;

	RubberAllocation allocation;
	size_t size;
	std::exception_ptr error;

	CancellablePointer cancel_ptr;

	explicit Data(Rubber &_r) noexcept:result(NONE), r(_r) {}

	/* virtual methods from class RubberSinkHandler */
	void RubberDone(RubberAllocation &&a, size_t size) noexcept override;
	void RubberOutOfMemory() noexcept override;
	void RubberTooLarge() noexcept override;
	void RubberError(std::exception_ptr ep) noexcept override;
};

void
Data::RubberDone(RubberAllocation &&a, size_t _size) noexcept
{
	assert(result == NONE);

	result = DONE;
	allocation = std::move(a);
	size = _size;
}

void
Data::RubberOutOfMemory() noexcept
{
	assert(result == NONE);

	result = OOM;
}

void
Data::RubberTooLarge() noexcept
{
	assert(result == NONE);

	result = TOO_LARGE;
}

void
Data::RubberError(std::exception_ptr ep) noexcept
{
	assert(result == NONE);

	result = ERROR;
	error = ep;
}

TEST(SinkRubberTest, Empty)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	sink_rubber_new(data.pool, istream_null_new(data.pool), r, 1024,
			data, data.cancel_ptr);

	ASSERT_EQ(Data::DONE, data.result);
	ASSERT_FALSE(data.allocation);
	ASSERT_EQ(size_t(0), data.size);
}

TEST(SinkRubberTest, Empty2)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	auto input = istream_byte_new(data.pool, istream_null_new(data.pool));
	auto *sink = sink_rubber_new(data.pool, std::move(input), r, 1024,
				     data, data.cancel_ptr);
	ASSERT_NE(sink, nullptr);

	ASSERT_EQ(Data::NONE, data.result);
	sink_rubber_read(*sink);

	ASSERT_EQ(Data::DONE, data.result);
	ASSERT_FALSE(data.allocation);
	ASSERT_EQ(size_t(0), data.size);
}

TEST(SinkRubberTest, String)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	auto input = istream_string_new(data.pool, "foo");
	auto *sink = sink_rubber_new(data.pool, std::move(input), r, 1024,
				     data, data.cancel_ptr);
	ASSERT_NE(sink, nullptr);

	ASSERT_EQ(Data::NONE, data.result);
	sink_rubber_read(*sink);

	ASSERT_EQ(Data::DONE, data.result);
	ASSERT_TRUE(data.allocation);
	ASSERT_EQ(size_t(3), data.size);
	ASSERT_EQ(size_t(32), r.GetSizeOf(data.allocation.GetId()));
	ASSERT_EQ(0, memcmp("foo", r.Read(data.allocation.GetId()), 3));
}

TEST(SinkRubberTest, String2)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	auto input = istream_four_new(data.pool,
				      istream_string_new(data.pool, "foobar"));
	auto *sink = sink_rubber_new(data.pool, std::move(input), r, 1024,
				     data, data.cancel_ptr);
	ASSERT_NE(sink, nullptr);

	ASSERT_EQ(Data::NONE, data.result);

	sink_rubber_read(*sink);
	if (Data::NONE == data.result)
		sink_rubber_read(*sink);

	ASSERT_EQ(Data::DONE, data.result);
	ASSERT_TRUE(data.allocation);
	ASSERT_EQ(size_t(6), data.size);
	ASSERT_EQ(size_t(32), r.GetSizeOf(data.allocation.GetId()));
	ASSERT_EQ(0, memcmp("foobar", r.Read(data.allocation.GetId()), 6));
}

TEST(SinkRubberTest, TooLarge1)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	sink_rubber_new(data.pool, istream_string_new(data.pool, "foobar"), r, 5,
			data, data.cancel_ptr);
	ASSERT_EQ(Data::TOO_LARGE, data.result);
}

TEST(SinkRubberTest, TooLarge2)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	auto input = istream_four_new(data.pool,
				      istream_string_new(data.pool, "foobar"));
	auto *sink = sink_rubber_new(data.pool, std::move(input), r, 5,
				     data, data.cancel_ptr);

	ASSERT_EQ(Data::NONE, data.result);

	sink_rubber_read(*sink);
	if (Data::NONE == data.result)
		sink_rubber_read(*sink);

	ASSERT_EQ(Data::TOO_LARGE, data.result);
}

TEST(SinkRubberTest, Error)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	auto input = istream_fail_new(data.pool,
				      std::make_exception_ptr(std::runtime_error("error")));
	auto *sink = sink_rubber_new(data.pool, std::move(input), r, 1024,
				     data, data.cancel_ptr);
	ASSERT_NE(sink, nullptr);

	ASSERT_EQ(Data::NONE, data.result);
	sink_rubber_read(*sink);

	ASSERT_EQ(Data::ERROR, data.result);
	ASSERT_NE(data.error, nullptr);
}

TEST(SinkRubberTest, OOM)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	EventLoop event_loop;
	auto input = istream_delayed_new(data.pool, event_loop);
	input.second.cancel_ptr = nullptr;

	sink_rubber_new(data.pool, std::move(input.first), r, 8 * 1024 * 1024,
			data, data.cancel_ptr);
	ASSERT_EQ(Data::OOM, data.result);
}

TEST(SinkRubberTest, Abort)
{
	Rubber r(4 * 1024 * 1024);
	Data data(r);

	EventLoop event_loop;
	auto delayed = istream_delayed_new(data.pool, event_loop);
	delayed.second.cancel_ptr = nullptr;

	auto input = istream_cat_new(data.pool,
				     istream_string_new(data.pool, "foo"),
				     std::move(delayed.first));
	auto *sink = sink_rubber_new(data.pool, std::move(input), r, 4,
				     data, data.cancel_ptr);
	ASSERT_NE(sink, nullptr);
	ASSERT_EQ(Data::NONE, data.result);
	sink_rubber_read(*sink);
	ASSERT_EQ(Data::NONE, data.result);

	data.cancel_ptr.Cancel();
}
