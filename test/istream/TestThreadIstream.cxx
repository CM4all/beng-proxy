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
#include "thread/Pool.hxx"
#include "memory/fb_pool.hxx"
#include "pool/pool.hxx"

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
