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

#define ISTREAM_TEST_NO_BIG

#include "IstreamFilterTest.hxx"
#include "istream/New.hxx"
#include "istream/BufferedIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/PipeLeaseIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"
#include "PipeLease.hxx"

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

	return std::move(hold);
}

class IstreamBufferedTestTraits {
public:
	static constexpr const char *expected_result = "foobar";

	static constexpr bool call_available = true;
	static constexpr bool got_data_assert = false;
	static constexpr bool enable_blocking = false;
	static constexpr bool enable_abort_istream = false;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		PipeLease pl(nullptr);
		pl.Create();
		pl.GetWriteFd().Write("bar", 3);

		return istream_cat_new(pool,
				       istream_string_new(pool, "foo"),
				       NewIstreamPtr<PipeLeaseIstream>(pool, std::move(pl), 3));
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return MakeBufferedIstream(pool, event_loop, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Buffered, IstreamFilterTest,
			      IstreamBufferedTestTraits);
