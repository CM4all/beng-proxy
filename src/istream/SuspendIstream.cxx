// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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

	off_t _GetAvailable(bool partial) noexcept override {
		return ready ? ForwardIstream::_GetAvailable(partial) : -1;
	}

	off_t _Skip(off_t length) noexcept override {
		return ready ? ForwardIstream::_Skip(length) : -1;
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
		else
			list.SetMore();
	}

	int _AsFd() noexcept override {
		return ready ? ForwardIstream::_AsFd() : -1;
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
