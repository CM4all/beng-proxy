// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/Chrono.hxx"

#include <cstdint>

class EventLoop;

/**
 * Tracks how much time is spent waiting for something, e.g. for more
 * data from the remote host.  This wait time shall be subtracted from
 * the wallclock duration of a transaction, in order to measure only
 * the time when progress was possible.
 *
 * Whenever the #waiting_mask is non-zero (i.e. at least one bit is
 * set), this class assumes we're waiting on an external resource.
 */
class WaitTracker {
	Event::Duration total = Event::Duration::zero();

	Event::TimePoint waiting_since;

public:
	using mask_t = uint_least8_t;

private:
	mask_t waiting_mask = 0;

public:
	constexpr void Reset() noexcept {
		total = Event::Duration::zero();
		waiting_mask = 0;
	}

	void Set(const EventLoop &event_loop, mask_t mask) noexcept;
	void Clear(const EventLoop &event_loop, mask_t mask) noexcept;

	/**
	 * @return the total duration in which the #waiting_mask was
	 * non-zero
	 */
	[[gnu::pure]]
	Event::Duration GetDuration(const EventLoop &event_loop) const noexcept;
};
