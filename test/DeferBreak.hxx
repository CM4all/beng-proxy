// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/DeferEvent.hxx"
#include "event/Loop.hxx"

class DeferBreak {
	DeferEvent event;

public:
	explicit DeferBreak(EventLoop &event_loop) noexcept
		:event(event_loop, BIND_THIS_METHOD(Break)) {}

	void ScheduleIdle() noexcept {
		event.ScheduleIdle();
	}

	void ScheduleNext() noexcept {
		event.ScheduleNext();
	}

private:
	void Break() noexcept {
		event.GetEventLoop().Break();
	}
};
