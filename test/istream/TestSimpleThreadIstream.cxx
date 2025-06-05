// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/SimpleThreadIstreamFilter.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_string.hxx"
#include "istream/ZeroIstream.hxx"
#include "thread/Pool.hxx"

#include <fmt/core.h>

#include <thread> // for std::this_thread::sleep_for()

/* most of this is a copy of TestThreadIstream.cxx refactored to use
   SimpleThreadIstreamFilter */

class NopSimpleThreadIstreamFilter final : public SimpleThreadIstreamFilter {
public:
	/* virtual methods from class SimpleThreadIstreamFilter */
	Result SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
			 Params) override {
		output.MoveFromAllowBothNull(input);
		return {.drained = true};
	}
};

class NopSimpleThreadIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
	};

	~NopSimpleThreadIstreamTestTraits() noexcept {
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
					std::make_unique<NopSimpleThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(SimpleThreadIstreamFilterNop, IstreamFilterTest,
			       NopSimpleThreadIstreamTestTraits);

/**
 * Like #NopSimpleThreadIstreamFilter, but inserts a header and a footer
 * byte.
 */
class FooSimpleThreadIstreamFilter final : public SimpleThreadIstreamFilter {
	bool header_sent = false, trailer_sent = false;

public:
	Result SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
			 Params params) override {
		if (!header_sent) {
			auto w = output.Write();
			if (w.empty())
				return {.drained = false};

			w.front() = std::byte{'H'};
			output.Append(1);

			header_sent = true;
		}

		/* sleep a bit to check whether main thread wakeups
		   work properly */
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		output.MoveFromAllowSrcNull(input);

		if (input.empty() && params.finish && !trailer_sent) {
			auto w = output.Write();
			if (w.empty())
				return {.drained = false};

			w.front() = std::byte{'T'};
			output.Append(1);

			trailer_sent = true;
		}

		return {.drained = trailer_sent};
	}
};

class FooSimpleThreadIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "HfoobarT",
	};

	~FooSimpleThreadIstreamTestTraits() noexcept {
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
					std::make_unique<FooSimpleThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(SimpleThreadIstreamFoo, IstreamFilterTest,
			       FooSimpleThreadIstreamTestTraits);

/**
 * A filter that returns each input byte 4093 times.  The goal is to
 * have stalls due to a full output buffer see how
 * #SimpleThreadIstreamFilter deals with this.
 */
class ExplodeSimpleThreadIstreamFilter final : public SimpleThreadIstreamFilter {
	std::size_t remaining = 0;

	std::byte value;

public:
	Result SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
			 Params) override {
		while (true) {
			if (remaining == 0) {
				const auto r = input.Read();
				if (r.empty())
					return {.drained = true};

				value = r.front();
				input.Consume(1);
				remaining = 4093;
			}

			auto w = output.Write();
			if (w.empty())
				return {.drained = false};

			std::size_t n = std::min(remaining, w.size());
			std::fill_n(w.begin(), n, value);
			output.Append(n);
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
					std::make_unique<ExplodeSimpleThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(SimpleThreadIstreamExplode, IstreamFilterTest,
			       ExplodeOutputIstreamTestTraits);

/**
 * A #SimpleThreadIstreamFilter implementation that counts all the
 * bytes sent to it and writes this number as string to the output.
 */
class CountSimpleThreadIstreamFilter final : public SimpleThreadIstreamFilter {
	std::size_t count = 0;

	bool first = true;

public:
	/* virtual methods from class SimpleThreadIstreamFilter */
	Result SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
			 Params params) override {
		if (first) {
			/* ignore the first run so both input buffers
			   get filled completely */
			first = false;
			return {.drained = false};
		}

		const auto r = input.Read();
		count += r.size();
		input.Consume(r.size());

		if (params.finish) {
			auto w = output.Write();
			assert(w.size() >= 64);

			auto *p = reinterpret_cast<char *>(w.data());
			char *q = fmt::format_to(p, "{}", count);
			output.Append(q - p);
		}

		return {.drained = params.finish};
	}
};

/**
 * Test with a huge input (but small output).  This checks whether
 * full input buffers can lead to stalled transfers.
 */
class HugeZeroInputIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "4194304",
		.enable_byte = false,
	};

	~HugeZeroInputIstreamTestTraits() noexcept {
		// invoke all pending ThreadJob::Done() calls
		if (event_loop_ != nullptr)
			event_loop_->Run();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_head_new(pool,
					istream_zero_new(pool),
					4194304, true);
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		event_loop_ = &event_loop;

		thread_pool_set_volatile();
		return NewThreadIstream(pool, thread_pool_get_queue(event_loop),
					std::move(input),
					std::make_unique<CountSimpleThreadIstreamFilter>());
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(SimpleThreadIstreamHugeZeroInput, IstreamFilterTest,
			       HugeZeroInputIstreamTestTraits);
