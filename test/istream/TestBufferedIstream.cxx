// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/New.hxx"
#include "istream/BufferedIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/PipeLeaseIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/SpanCast.hxx"

using std::string_view_literals::operator""sv;

class BufferedIstreamAdapter final : public BufferedIstreamHandler, Cancellable {
	DelayedIstreamControl &delayed;

public:
	CancellablePointer cancel_ptr;

	explicit BufferedIstreamAdapter(DelayedIstreamControl &_delayed) noexcept
		:delayed(_delayed)
	{
		delayed.cancel_ptr = *this;
	}

private:
	void Destroy() noexcept {
		this->~BufferedIstreamAdapter();
	}

	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	void OnBufferedIstreamReady(UnusedIstreamPtr i) noexcept override {
		auto &d = delayed;
		Destroy();
		d.Set(std::move(i));
	}

	void OnBufferedIstreamError(std::exception_ptr e) noexcept override {
		auto &d = delayed;
		Destroy();
		d.SetError(std::move(e));
	}
};

static UnusedIstreamPtr
MakeBufferedIstream(struct pool &pool, EventLoop &event_loop,
		    UnusedIstreamPtr input) noexcept
{
	auto delayed = istream_delayed_new(pool, event_loop);
	UnusedHoldIstreamPtr hold(pool, std::move(delayed.first));

	auto *adapter = NewFromPool<BufferedIstreamAdapter>(pool,
							    delayed.second);
	NewBufferedIstream(pool, event_loop, nullptr,
			   *adapter, std::move(input),
			   adapter->cancel_ptr);

	return hold;
}

class IstreamBufferedTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
		.enable_blocking = false,
		.enable_abort_istream = false,
		.enable_big = false,
		.enable_buckets_second_fail = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		PipeLease pl(nullptr);
		pl.Create();
		pl.GetWriteFd().Write(AsBytes("bar"sv));

		return NewConcatIstream(pool,
					istream_string_new(pool, "foo"),
					NewIstreamPtr<PipeLeaseIstream>(pool, std::move(pl), 3));
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return MakeBufferedIstream(pool, event_loop, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Buffered, IstreamFilterTest,
			       IstreamBufferedTestTraits);
