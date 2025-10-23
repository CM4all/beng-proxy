// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

	IstreamLength _GetLength() noexcept override {
		return {.exhaustive = false};
	}

	off_t _Skip(off_t) noexcept override {
		return -1;
	}

	void _Read() noexcept override {
		Schedule();
	}

	/* virtual methods from class IstreamHandler */

	void OnEof() noexcept override {
		ClearInput();
		Schedule();
	}

	void OnError(std::exception_ptr &&ep) noexcept override {
		ForwardIstream::OnError(std::move(ep));
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
