// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ForwardIstream.hxx"
#include "event/DeferEvent.hxx"

class DeferReadIstream final : public ForwardIstream {
	DeferEvent defer_event;

public:
	DeferReadIstream(struct pool &p, EventLoop &event_loop,
			 UnusedIstreamPtr &&_input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 defer_event(event_loop, BIND_THIS_METHOD(OnDeferEvent))
	{
		defer_event.Schedule();
	}

private:
	void OnDeferEvent() noexcept {
		Read();
	}
};
