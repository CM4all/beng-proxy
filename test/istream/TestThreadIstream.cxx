// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/ThreadIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Handler.hxx"
#include "istream/New.hxx"
#include "istream/ZeroIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/istream_null.hxx"
#include "thread/Pool.hxx"
#include "memory/fb_pool.hxx"
#include "pool/pool.hxx"

#include <algorithm>
#include <chrono>
#include <thread>

using std::string_view_literals::operator""sv;

/**
 * No-op filter that copies data as-is.
 */
class NopThreadIstreamFilter final : public ThreadIstreamFilter {
public:
	void Run(ThreadIstreamInternal &i) override {
		const std::scoped_lock lock{i.mutex};
		i.output.MoveFromAllowBothNull(i.input);
	}
};

class NopThreadIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
		.enable_buckets_second_fail = false,
	};

	~NopThreadIstreamTestTraits() noexcept {
		// invoke all pending ThreadJob::Done() calls
		if (event_loop_ != nullptr)
			event_loop_->Run();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		event_loop_ = &event_loop;

		thread_pool_set_volatile();
		return NewThreadIstream(pool, thread_pool_get_queue(event_loop),
					std::move(input),
					std::make_unique<NopThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(ThreadIstreamNop, IstreamFilterTest,
			       NopThreadIstreamTestTraits);

/**
 * Like #NopThreadIstreamFilter, but inserts a header and a footer
 * byte.
 */
class FooThreadIstreamFilter final : public ThreadIstreamFilter {
	bool header_sent = false, trailer_sent = false;

public:
	void Run(ThreadIstreamInternal &i) override {
		if (!header_sent) {
			const std::scoped_lock lock{i.mutex};
			auto w = i.output.Write();
			if (w.empty()) {
				i.again = true;
				return;
			}

			w.front() = std::byte{'H'};
			i.output.Append(1);

			header_sent = true;
		}

		/* sleep a bit to check whether main thread wakeups
		   work properly */
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		{
			const std::scoped_lock lock{i.mutex};
			i.output.MoveFromAllowSrcNull(i.input);
		}

		if (!i.has_input && i.input.empty() && !trailer_sent) {
			const std::scoped_lock lock{i.mutex};

			auto w = i.output.Write();
			if (w.empty()) {
				i.again = true;
				return;
			}

			w.front() = std::byte{'T'};
			i.output.Append(1);

			trailer_sent = true;
		}
	}
};

class FooThreadIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "HfoobarT",
		.enable_buckets_second_fail = false,
	};

	~FooThreadIstreamTestTraits() noexcept {
		// invoke all pending ThreadJob::Done() calls
		if (event_loop_ != nullptr)
			event_loop_->Run();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		event_loop_ = &event_loop;

		thread_pool_set_volatile();
		return NewThreadIstream(pool, thread_pool_get_queue(event_loop),
					std::move(input),
					std::make_unique<FooThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(ThreadIstreamFoo, IstreamFilterTest,
			       FooThreadIstreamTestTraits);

/**
 * A filter that returns each input byte 4093 times.  The goal is to
 * have stalls due to a full output buffer see how #ThreadIstream
 * deals with this.
 */
class ExplodeThreadIstreamFilter final : public ThreadIstreamFilter {
	std::size_t remaining = 0;

	std::byte value;

public:
	void Run(ThreadIstreamInternal &i) override {
		const std::scoped_lock lock{i.mutex};

		while (true) {
			if (remaining == 0) {
				const auto r = i.input.Read();
				if (r.empty()) {
					i.drained = true;
					return;
				}

				value = r.front();
				i.input.Consume(1);
				remaining = 4093;
			}

			auto w = i.output.Write();
			if (w.empty()) {
				i.drained = false;
				i.again = true;
				return;
			}

			std::size_t n = std::min(remaining, w.size());
			std::fill_n(w.begin(), n, value);
			i.output.Append(n);
			remaining -= n;
		}
	}
};

template<std::size_t size>
static constexpr auto MakeExplodedBuffer(const char (&src)[size]) noexcept {
	constexpr std::size_t length = size - 1;
	std::array<char, length * 4093 + 1> buffer{};
	auto o = buffer.begin();
	for (std::size_t i = 0; i < length; ++i)
		o = std::fill_n(o, 4093, src[i]);
	*o = '\0';

	return buffer;
}

class ExplodeOutputIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

	static constexpr char input_string[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	static constexpr auto result = MakeExplodedBuffer(input_string);

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = result.data(),
		.enable_buckets_second_fail = false,
	};

	~ExplodeOutputIstreamTestTraits() noexcept {
		// invoke all pending ThreadJob::Done() calls
		if (event_loop_ != nullptr)
			event_loop_->Run();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, input_string);
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		event_loop_ = &event_loop;

		thread_pool_set_volatile();
		return NewThreadIstream(pool, thread_pool_get_queue(event_loop),
					std::move(input),
					std::make_unique<ExplodeThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(ThreadIstreamExplode, IstreamFilterTest,
			       ExplodeOutputIstreamTestTraits);

/**
 * Filter that copies data as-is, but goes through an internal buffer
 * that is not "drained".
 */
class DrainThreadIstreamFilter final : public ThreadIstreamFilter {
	SliceFifoBuffer output;

public:
	bool PreRun(ThreadIstreamInternal &) noexcept override {
		if (!output.IsDefined())
			output.AllocateIfNull(fb_pool_get());
		return true;
	}

	void Run(ThreadIstreamInternal &i) override {
		bool was_empty;

		{
			const std::scoped_lock lock{i.mutex};

			if (!output.IsDefined()) {
				i.again = true;
				return;
			}

			was_empty = output.empty() && !i.input.empty();
			if (was_empty)
				output.MoveFromAllowBothNull(i.input);
			else if (!i.input.empty())
				i.again = true;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		{
			const std::scoped_lock lock{i.mutex};

			if (!was_empty)
				i.output.MoveFromAllowBothNull(output);
			else if (!output.empty())
				i.again = true;

			i.drained = output.empty();
		}
	}

	void PostRun(ThreadIstreamInternal &) noexcept override {
		output.FreeIfEmpty();
	}
};

class DrainThreadIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
		.enable_buckets_second_fail = false,
	};

	~DrainThreadIstreamTestTraits() noexcept {
		// invoke all pending ThreadJob::Done() calls
		if (event_loop_ != nullptr)
			event_loop_->Run();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		event_loop_ = &event_loop;

		thread_pool_set_volatile();
		return NewThreadIstream(pool, thread_pool_get_queue(event_loop),
					std::move(input),
					std::make_unique<DrainThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(ThreadIstreamDrain, IstreamFilterTest,
			       DrainThreadIstreamTestTraits);

/**
 * Like #DrainThreadIstreamFilter, but finish the buffer only after
 * the input reaches end-of-file.
 */
class FinishThreadIstreamFilter final : public ThreadIstreamFilter {
	SliceFifoBuffer output;

public:
	void Run(ThreadIstreamInternal &i) override {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		const std::scoped_lock lock{i.mutex};
		output.MoveFromAllowSrcNull(i.input);

		if (!i.has_input && i.input.empty())
			i.output.MoveFromAllowBothNull(output);

		i.drained = output.empty();
	}

	void PostRun(ThreadIstreamInternal &) noexcept override {
		output.FreeIfEmpty();
	}
};

class FinishThreadIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
		.enable_buckets_second_fail = false,
		.late_finish = true,
	};

	~FinishThreadIstreamTestTraits() noexcept {
		// invoke all pending ThreadJob::Done() calls
		if (event_loop_ != nullptr)
			event_loop_->Run();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		event_loop_ = &event_loop;

		thread_pool_set_volatile();
		return NewThreadIstream(pool, thread_pool_get_queue(event_loop),
					std::move(input),
					std::make_unique<FinishThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(ThreadIstreamFinish, IstreamFilterTest,
			       FinishThreadIstreamTestTraits);

/**
 * A filter which ignores its input and emits a fixed number of filler
 * bytes.  It is "drained" only after all of them have been written to
 * the #ThreadIstreamInternal output buffer.
 */
class FillerThreadIstreamFilter final : public ThreadIstreamFilter {
	std::size_t remaining;

public:
	explicit FillerThreadIstreamFilter(std::size_t _remaining) noexcept
		:remaining(_remaining) {}

	void Run(ThreadIstreamInternal &i) override {
		const std::scoped_lock lock{i.mutex};

		auto w = i.output.Write();
		const std::size_t n = std::min(w.size(), remaining);
		std::fill_n(w.begin(), n, std::byte{'x'});
		i.output.Append(n);
		remaining -= n;

		i.drained = remaining == 0;

		/* if there is more to write, run again (even if the
		   output buffer is full right now - then Done() will
		   leave "output_full" set and OutputConsumed() will
		   reschedule us) */
		i.again = remaining > 0;
	}
};

/**
 * An #IstreamSink which refuses all data until Accept() is called.
 */
class BlockingSink final : IstreamSink {
	std::size_t accept = 0;

public:
	std::size_t consumed = 0;
	bool eof = false;
	std::exception_ptr error;

	explicit BlockingSink(UnusedIstreamPtr _input) noexcept
		:IstreamSink(std::move(_input)) {}

	using IstreamSink::HasInput;

	void Read() noexcept {
		input.Read();
	}

	/**
	 * Allow the sink to consume up to the given number of bytes
	 * on the next OnData() call.
	 */
	void Accept(std::size_t n) noexcept {
		accept = n;
	}

private:
	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		const std::size_t n = std::min(accept, src.size());
		accept -= n;
		consumed += n;
		return n;
	}

	void OnEof() noexcept override {
		ClearInput();
		eof = true;
	}

	void OnError(std::exception_ptr &&_error) noexcept override {
		ClearInput();
		error = std::move(_error);
	}
};

/**
 * Releasing the #ThreadIstreamFilter must not leave the job
 * schedulable; scheduling it again would dereference the null filter.
 *
 * To get there, the filter must finish at a moment when the
 * #ThreadIstream's own output buffer is full (so Done() cannot move
 * anything out of the internal output buffer, which therefore stays
 * full) - hence the sink which refuses all data, and a filler size of
 * two buffers.
 */
TEST(ThreadIstream, ReleaseFilterWithFullOutput)
{
	Instance instance;

	thread_pool_set_volatile();

	{
		const auto pool = pool_new_linear(instance.root_pool, "test", 8192);

		BlockingSink sink{
			NewThreadIstream(pool, thread_pool_get_queue(instance.event_loop),
					 istream_null_new(pool),
					 std::make_unique<FillerThreadIstreamFilter>(2 * FB_SIZE)),
		};

		/* let the filter run; the sink refuses all data, so
		   both output buffers end up full and the filter is
		   released while its output buffer is still full */
		sink.Read();
		instance.event_loop.Run();

		ASSERT_TRUE(sink.HasInput());
		ASSERT_EQ(sink.consumed, 0u);

		/* now consume a little; this used to reschedule the
		   job which had no filter anymore */
		sink.Accept(1024);
		sink.Read();
		instance.event_loop.Run();

		EXPECT_EQ(sink.consumed, 1024u);
		EXPECT_FALSE(sink.error);
	}

	instance.event_loop.Run();

	thread_pool_stop();
	thread_pool_join();
	thread_pool_deinit();
}
