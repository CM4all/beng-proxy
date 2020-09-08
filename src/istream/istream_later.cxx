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

#include "istream_later.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "event/DeferEvent.hxx"

class LaterIstream final : public ForwardIstream {
	DeferEvent defer_event;

public:
	LaterIstream(struct pool &_pool, UnusedIstreamPtr &&_input,
		     EventLoop &event_loop)
		:ForwardIstream(_pool, std::move(_input)),
		 defer_event(event_loop, BIND_THIS_METHOD(OnDeferred))
	{
	}

	/* virtual methods from class Istream */

	off_t _GetAvailable(gcc_unused bool partial) noexcept override {
		return -1;
	}

	off_t _Skip(gcc_unused off_t length) noexcept override {
		return -1;
	}

	void _Read() noexcept override {
		Schedule();
	}

	int _AsFd() noexcept override {
		return -1;
	}

	void _Close() noexcept override {
		/* input can only be nullptr during the eof callback delay */
		if (HasInput())
			input.Close();

		Destroy();
	}

	/* virtual methods from class IstreamHandler */

	void OnEof() noexcept override {
		ClearInput();
		Schedule();
	}

	void OnError(std::exception_ptr ep) noexcept override {
		ForwardIstream::OnError(ep);
	}

private:
	void Schedule() noexcept {
		defer_event.Schedule();
	}

	void OnDeferred() noexcept {
		if (!HasInput())
			DestroyEof();
		else
			ForwardIstream::_Read();
	}
};

UnusedIstreamPtr
istream_later_new(struct pool &pool, UnusedIstreamPtr input,
		  EventLoop &event_loop) noexcept
{
	return NewIstreamPtr<LaterIstream>(pool, std::move(input), event_loop);
}
