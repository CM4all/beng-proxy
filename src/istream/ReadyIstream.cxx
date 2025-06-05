// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ReadyIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "event/DeferEvent.hxx"

class ReadyIstream final : public ForwardIstream {
	DeferEvent defer_ready;

	bool fallback = false;

public:
	ReadyIstream(struct pool &p, UnusedIstreamPtr &&_input,
		     EventLoop &event_loop) noexcept
		:ForwardIstream(p, std::move(_input)),
		 defer_ready(event_loop, BIND_THIS_METHOD(OnDeferredReady)) {}

	/* handler */
	size_t OnData(std::span<const std::byte> src) noexcept override;

private:
	void OnDeferredReady() noexcept;
};

size_t
ReadyIstream::OnData(std::span<const std::byte> src) noexcept
{
	if (fallback) {
		fallback = false;
		return ForwardIstream::OnData(src);
	}

	defer_ready.Schedule();
	return 0;
}

void
ReadyIstream::OnDeferredReady() noexcept
{
	switch (InvokeReady()) {
	case IstreamReadyResult::OK:
		break;

	case IstreamReadyResult::FALLBACK:
		fallback = true;
		input.Read();
		break;

	case IstreamReadyResult::CLOSED:
		break;
	}
}

UnusedIstreamPtr
NewReadyIstream(EventLoop &event_loop,
		struct pool &pool, UnusedIstreamPtr input) noexcept
{
	return NewIstreamPtr<ReadyIstream>(pool, std::move(input), event_loop);
}
