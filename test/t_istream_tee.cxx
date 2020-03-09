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

#include "PInstance.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Sink.hxx"
#include "istream/TeeIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/FailIstream.hxx"
#include "istream/istream.hxx"
#include "istream/sink_close.hxx"
#include "istream/StringSink.hxx"
#include "istream/Bucket.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"

#include <memory>

#include <string.h>

struct StatsIstreamSink : IstreamSink {
	size_t total_data = 0;
	bool eof = false;
	std::exception_ptr error;

	template<typename I>
	explicit StatsIstreamSink(I &&_input) noexcept
		:IstreamSink(std::forward<I>(_input)) {}

	/* only here to work around -Wdelete-non-virtual-dtor */
	virtual ~StatsIstreamSink() = default;

	using IstreamSink::ClearAndCloseInput;

	void Read() noexcept {
		input.Read();
	}

	void FillBucketList(IstreamBucketList &list) {
		input.FillBucketList(list);
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(gcc_unused const void *data, size_t length) noexcept override {
		total_data += length;
		return length;
	}

	void OnEof() noexcept override {
		eof = true;
	}

	void OnError(std::exception_ptr ep) noexcept override {
		error = ep;
	}
};

struct Context : StringSinkHandler {
	std::string value;

	void OnStringSinkSuccess(std::string &&_value) noexcept final {
		value = std::move(_value);
	}

	void OnStringSinkError(std::exception_ptr) noexcept final {
	}
};

struct BlockContext final : Context, StatsIstreamSink {
	template<typename I>
	explicit BlockContext(I &&_input) noexcept
		:StatsIstreamSink(std::forward<I>(_input)) {}

	/* istream handler */

	size_t OnData(gcc_unused const void *data, gcc_unused size_t length) noexcept override {
		// block
		return 0;
	}
};

/*
 * tests
 *
 */

static void
test_block1(EventLoop &event_loop)
{
	CancellablePointer cancel_ptr;

	auto pool = pool_new_libc(nullptr, "test");

	auto delayed = istream_delayed_new(*pool, event_loop);
	auto tee1 = NewTeeIstream(*pool, std::move(delayed.first),
				  event_loop, false);
	auto tee2 = AddTeeIstream(tee1, false);

	BlockContext ctx(std::move(tee1));

	auto &sink = NewStringSink(*pool, std::move(tee2), ctx, cancel_ptr);
	assert(ctx.value.empty());

	/* the input (istream_delayed) blocks */
	ReadStringSink(sink);
	assert(ctx.value.empty());

	/* feed data into input */
	delayed.second.Set(istream_string_new(*pool, "foo"));
	assert(ctx.value.empty());

	/* the first output (block_istream_handler) blocks */
	ReadStringSink(sink);
	assert(ctx.value.empty());

	/* close the blocking output, this should release the "tee"
	   object and restart reading (into the second output) */
	assert(ctx.error == nullptr && !ctx.eof);
	ctx.ClearAndCloseInput();
	event_loop.LoopOnceNonBlock();

	assert(ctx.error == nullptr && !ctx.eof);
	assert(strcmp(ctx.value.c_str(), "foo") == 0);

	pool.reset();
	pool_commit();
}

static void
test_close_data(EventLoop &event_loop, struct pool *_pool)
{
	Context ctx;
	CancellablePointer cancel_ptr;

	auto pool = pool_new_libc(_pool, "test");
	auto tee1 = NewTeeIstream(*pool, istream_string_new(*pool, "foo"),
				  event_loop, false);
	auto tee2 = AddTeeIstream(tee1, false);

	sink_close_new(*pool, std::move(tee1));

	auto &sink = NewStringSink(*pool, std::move(tee2), ctx, cancel_ptr);
	assert(ctx.value.empty());

	ReadStringSink(sink);

	/* at this point, sink_close has closed itself, and istream_tee
	   should have passed the data to the StringSink */

	assert(strcmp(ctx.value.c_str(), "foo") == 0);

	pool_commit();
}

/**
 * Close the second output after data has been consumed only by the
 * first output.  This verifies that istream_tee's "skip" attribute is
 * obeyed properly.
 */
static void
test_close_skipped(EventLoop &event_loop, struct pool *_pool)
{
	Context ctx;
	CancellablePointer cancel_ptr;

	auto pool = pool_new_libc(_pool, "test");
	auto tee1 = NewTeeIstream(*pool, istream_string_new(*pool, "foo"),
				  event_loop, false);
	auto tee2 = AddTeeIstream(tee1, false);
	auto &sink = NewStringSink(*pool, std::move(tee1), ctx, cancel_ptr);

	sink_close_new(*pool, std::move(tee2));

	assert(ctx.value.empty());

	ReadStringSink(sink);

	assert(strcmp(ctx.value.c_str(), "foo") == 0);

	pool_commit();
}

static void
test_error(EventLoop &event_loop, struct pool *_pool,
	   bool close_first, bool close_second,
	   bool read_first)
{
	auto pool = pool_new_libc(_pool, "test");
	auto tee1 =
		NewTeeIstream(*pool, istream_fail_new(*pool,
						      std::make_exception_ptr(std::runtime_error("error"))),
			      event_loop,
			      false);
	auto tee2 = AddTeeIstream(tee1, false);
	pool.reset();

	auto first = !close_first
		? std::make_unique<StatsIstreamSink>(std::move(tee1))
		: nullptr;
	if (close_first)
		tee1.Clear();

	auto second = !close_second
		? std::make_unique<StatsIstreamSink>(std::move(tee2))
		: nullptr;
	if (close_second)
		tee2.Clear();

	if (read_first)
		first->Read();
	else
		second->Read();

	if (!close_first) {
		assert(first->total_data == 0);
		assert(!first->eof);
		assert(first->error != nullptr);
	}

	if (!close_second) {
		assert(second->total_data == 0);
		assert(!second->eof);
		assert(second->error != nullptr);
	}

	pool_commit();
}

static void
test_bucket_error(EventLoop &event_loop, struct pool *_pool,
		  bool close_second_early,
		  bool close_second_late)
{
	auto pool = pool_new_libc(_pool, "test");
	auto tee1 =
		NewTeeIstream(*pool, istream_fail_new(*pool,
						      std::make_exception_ptr(std::runtime_error("error"))),
			      event_loop,
			      false);
	auto tee2 = AddTeeIstream(tee1, false);
	pool.reset();

	StatsIstreamSink first(std::move(tee1));

	auto second = !close_second_late
		? std::make_unique<StatsIstreamSink>(std::move(tee2))
		: nullptr;
	if (close_second_early) {
		if (second)
			second->ClearAndCloseInput();
		else
			tee2.Clear();
	}

	IstreamBucketList list;

	try {
		first.FillBucketList(list);
		assert(false);
	} catch (...) {
		assert(strcmp(GetFullMessage(std::current_exception()).c_str(),
			      "error") == 0);
	}

	if (close_second_late)
		tee2.Clear();

	if (!close_second_early && !close_second_late) {
		second->Read();
		assert(second->total_data == 0);
		assert(!second->eof);
		assert(second->error != nullptr);
	}

	pool_commit();
}

/*
 * main
 *
 */


int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	PInstance instance;

	/* run test suite */

	test_block1(instance.event_loop);
	test_close_data(instance.event_loop, instance.root_pool);
	test_close_skipped(instance.event_loop, instance.root_pool);
	test_error(instance.event_loop, instance.root_pool, false, false, true);
	test_error(instance.event_loop, instance.root_pool, false, false, false);
	test_error(instance.event_loop, instance.root_pool, true, false, false);
	test_error(instance.event_loop, instance.root_pool, false, true, true);
	test_bucket_error(instance.event_loop, instance.root_pool, false, false);
	test_bucket_error(instance.event_loop, instance.root_pool, true, false);
	test_bucket_error(instance.event_loop, instance.root_pool, false, true);
}
