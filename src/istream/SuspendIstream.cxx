// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SuspendIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "Bucket.hxx"
#include "event/FineTimerEvent.hxx"

class SuspendIstream final : public ForwardIstream {
	FineTimerEvent timer;

	const Event::Duration delay;

	bool ready = false;

public:
	SuspendIstream(struct pool &_pool, UnusedIstreamPtr &&_input,
		       EventLoop &event_loop, Event::Duration _delay)
		:ForwardIstream(_pool, std::move(_input)),
		 timer(event_loop, BIND_THIS_METHOD(OnTimer)),
		 delay(_delay)
	{
	}

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override {
		return ready ? ForwardIstream::_GetLength() : IstreamLength{.exhaustive = false};
	}

	void _Read() noexcept override {
		if (ready)
			ForwardIstream::_Read();
		else
			Schedule();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (ready)
			ForwardIstream::_FillBucketList(list);
		else {
			list.SetPushMore();
			Schedule();
		}
	}

private:
	void Schedule() noexcept {
		if (!timer.IsPending())
			timer.Schedule(delay);
	}

	void OnTimer() noexcept {
		ready = true;

		switch (InvokeReady()) {
		case IstreamReadyResult::OK:
			break;

		case IstreamReadyResult::FALLBACK:
			input.Read();
			break;

		case IstreamReadyResult::CLOSED:
			break;
		}
	}
};

UnusedIstreamPtr
NewSuspendIstream(struct pool &pool, UnusedIstreamPtr input,
		  EventLoop &event_loop, Event::Duration delay) noexcept
{
	return NewIstreamPtr<SuspendIstream>(pool, std::move(input),
					     event_loop, delay);
}
