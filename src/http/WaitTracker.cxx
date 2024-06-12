// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "WaitTracker.hxx"
#include "event/Loop.hxx"

#include <cassert>

void
WaitTracker::Set(const EventLoop &event_loop, mask_t mask) noexcept
{
	assert(mask != 0);

	if (waiting_mask == 0)
		waiting_since = event_loop.SteadyNow();

	waiting_mask |= mask;
}

void
WaitTracker::Clear(const EventLoop &event_loop, mask_t mask) noexcept
{
	assert(mask != 0);

	if ((waiting_mask & mask) == 0) [[unlikely]]
		return;

	waiting_mask &= ~mask;

	if (waiting_mask == 0)
		total += event_loop.SteadyNow() - waiting_since;
}

Event::Duration
WaitTracker::GetDuration(const EventLoop &event_loop) const noexcept {
	Event::Duration duration = total;
	if (waiting_mask != 0)
		duration += event_loop.SteadyNow() - waiting_since;
	return duration;
}
