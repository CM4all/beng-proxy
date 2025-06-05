// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TimeoutIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "net/TimeoutError.hxx"
#include "io/FileDescriptor.hxx"

class TimeoutIstream final : public ForwardIstream {
	CoarseTimerEvent timeout_event;

	Event::Duration timeout;

public:
	explicit TimeoutIstream(struct pool &p, UnusedIstreamPtr _input,
				EventLoop &event_loop,
				Event::Duration _timeout) noexcept
		:ForwardIstream(p, std::move(_input)),
		 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
		 timeout(_timeout) {}

private:
	void OnTimeout() noexcept {
		DestroyError(std::make_exception_ptr(TimeoutError{}));
	}

public:
	/* virtual methods from class Istream */

	void _Read() noexcept override {
		if (timeout > Event::Duration{}) {
			/* enable the timeout on the first Read() call (if one was
			   specified) */
			timeout_event.Schedule(timeout);
			timeout = Event::Duration{};
		}

		ForwardIstream::_Read();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		IstreamBucketList tmp;
		ForwardIstream::_FillBucketList(tmp);

		if (!tmp.IsEmpty())
			/* disable the timeout as soon as the first data byte
			   arrives */
			timeout_event.Cancel();

		list.SpliceFrom(std::move(tmp));
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		/* disable the timeout as soon as the first data byte
		   arrives */
		timeout_event.Cancel();

		return ForwardIstream::OnData(src);
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override {
		/* disable the timeout as soon as the first data byte
		   arrives */
		timeout_event.Cancel();

		return ForwardIstream::OnDirect(type, fd, offset, max_length,
						then_eof);
	}
};

UnusedIstreamPtr
NewTimeoutIstream(struct pool &pool, UnusedIstreamPtr input,
		  EventLoop &event_loop,
		  Event::Duration timeout) noexcept
{
	return NewIstreamPtr<TimeoutIstream>(pool, std::move(input),
					     event_loop, timeout);
}
