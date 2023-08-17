// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HalfSuspendIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "Bucket.hxx"
#include "event/FineTimerEvent.hxx"

class HalfSuspendIstream final : public ForwardIstream {
	FineTimerEvent timer;

	const Event::Duration delay;

	bool ready = false;

public:
	HalfSuspendIstream(struct pool &_pool, UnusedIstreamPtr &&_input,
		       EventLoop &event_loop, Event::Duration _delay)
		:ForwardIstream(_pool, std::move(_input)),
		 timer(event_loop, BIND_THIS_METHOD(OnTimer)),
		 delay(_delay)
	{
	}

	/* virtual methods from class Istream */
	void _FillBucketList(IstreamBucketList &list) override {
		if (ready) {
			ForwardIstream::_FillBucketList(list);
		} else if (timer.IsPending()) {
			list.SetMore();
		} else {
			timer.Schedule(delay);

			IstreamBucketList tmp;
			ForwardIstream::_FillBucketList(tmp);

			list.SpliceBuffersFrom(std::move(tmp),
					       (tmp.GetTotalBufferSize() + 1) / 2);
		}
	}

	/* virtual methods from class Istream */
	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		if (ready)
			return ForwardIstream::OnData(src);
		else if (timer.IsPending())
			return 0;
		else {
			src = src.first((src.size() + 1) / 2);
			const std::size_t nbytes = ForwardIstream::OnData(src);
			if (nbytes > 0)
				timer.Schedule(delay);
			return nbytes;
		}
	}

private:
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
NewHalfSuspendIstream(struct pool &pool, UnusedIstreamPtr input,
		  EventLoop &event_loop, Event::Duration delay) noexcept
{
	return NewIstreamPtr<HalfSuspendIstream>(pool, std::move(input),
						 event_loop, delay);
}
