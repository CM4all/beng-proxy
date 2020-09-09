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

#include "SuspendIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "event/TimerEvent.hxx"

class SuspendIstream final : public ForwardIstream {
	TimerEvent timer;

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
		input.Read();
	}
};

UnusedIstreamPtr
NewSuspendIstream(struct pool &pool, UnusedIstreamPtr input,
		  EventLoop &event_loop, Event::Duration delay) noexcept
{
	return NewIstreamPtr<SuspendIstream>(pool, std::move(input),
					     event_loop, delay);
}
