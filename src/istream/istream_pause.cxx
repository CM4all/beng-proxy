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

#include "istream_pause.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "event/DeferEvent.hxx"

class PauseIstream final : public ForwardIstream {
	const SharedPoolPtr<PauseIstreamControl> control;

	DeferEvent defer_read;

	bool want_read = false;

	bool resumed = false;

public:
	PauseIstream(struct pool &p, EventLoop &event_loop,
		     UnusedIstreamPtr _input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 control(SharedPoolPtr<PauseIstreamControl>::Make(p, *this)),
		 defer_read(event_loop, BIND_THIS_METHOD(DeferredRead)) {}

	~PauseIstream() noexcept {
		control->pause = nullptr;
	}

	auto GetControl() noexcept {
		return control;
	}

	void Resume() noexcept {
		resumed = true;

		if (want_read)
			/* there is a pending read request; schedule it to be
			   executed (but outside of this stack frame) */
			defer_read.Schedule();
	}

private:
	void DeferredRead() noexcept {
		ForwardIstream::_Read();
	}

protected:
	/* virtual methods from class Istream */

	void _Read() noexcept override {
		if (resumed) {
			defer_read.Cancel();
			ForwardIstream::_Read();
		} else {
			/* we'll try again after Resume() gets called */
			want_read = true;
		}
	}

	int _AsFd() noexcept override {
		return resumed
			? ForwardIstream::_AsFd()
			: -1;
	}
};

void
PauseIstreamControl::Resume() noexcept
{
	if (pause != nullptr)
		pause->Resume();
}

std::pair<UnusedIstreamPtr, SharedPoolPtr<PauseIstreamControl>>
istream_pause_new(struct pool &pool, EventLoop &event_loop,
		  UnusedIstreamPtr input) noexcept
{
	auto *i = NewIstream<PauseIstream>(pool, event_loop, std::move(input));
	return std::make_pair(UnusedIstreamPtr(i), i->GetControl());
}
