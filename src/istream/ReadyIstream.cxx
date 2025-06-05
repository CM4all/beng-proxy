// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ReadyIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "event/DeferEvent.hxx"

class ReadyIstream final : public ForwardIstream {
	DeferEvent defer_ready;

	bool allow_buckets = false;

	bool fallback = false;

public:
	ReadyIstream(struct pool &p, UnusedIstreamPtr &&_input,
		     EventLoop &event_loop) noexcept
		:ForwardIstream(p, std::move(_input)),
		 defer_ready(event_loop, BIND_THIS_METHOD(OnDeferredReady)) {}

	// virtual methods from Istream
	void _FillBucketList(IstreamBucketList &list) override;

	// virtual methods from IstreamHandler
	size_t OnData(std::span<const std::byte> src) noexcept override;

private:
	void OnDeferredReady() noexcept;
};

void
ReadyIstream::_FillBucketList(IstreamBucketList &list)
{
	if (!allow_buckets) {
		defer_ready.Schedule();
		list.SetMore();
		return;
	}

	return ForwardIstream::_FillBucketList(list);
}

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
	allow_buckets = true;

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
