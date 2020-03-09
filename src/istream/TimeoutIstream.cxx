/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "TimeoutIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "event/TimerEvent.hxx"

#include <stdexcept>

class TimeoutIstream final : public ForwardIstream {
	TimerEvent timeout_event;

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
		input.Close();
		DestroyError(std::make_exception_ptr(std::runtime_error("timeout")));
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

		try {
			input.FillBucketList(tmp);
		} catch (...) {
			Destroy();
			throw;
		}

		if (!tmp.IsEmpty())
			/* disable the timeout as soon as the first data byte
			   arrives */
			timeout_event.Cancel();

		list.SpliceBuffersFrom(tmp);
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(const void *data, size_t length) noexcept override {
		/* disable the timeout as soon as the first data byte
		   arrives */
		timeout_event.Cancel();

		return ForwardIstream::OnData(data, length);
	}

	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override {
		/* disable the timeout as soon as the first data byte
		   arrives */
		timeout_event.Cancel();

		return ForwardIstream::OnDirect(type, fd, max_length);
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
