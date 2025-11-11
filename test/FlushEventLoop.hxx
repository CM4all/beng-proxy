// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "DeferBreak.hxx"
#include "event/Loop.hxx"

/**
 * Dispatch all pending events in the #EventLoop, i.e. do not call
 * epoll_wait().
 */
inline void
FlushPending(EventLoop &event_loop) noexcept
{
	DeferBreak b{event_loop};
	b.ScheduleNext();
	event_loop.Run();
}

/**
 * Dispatch all pending I/O events in the #EventLoop, i.e. calls
 * epoll_wait() once with a zero timeout.
 */
inline void
FlushIO(EventLoop &event_loop) noexcept
{
	DeferBreak b{event_loop};
	b.ScheduleNext();
	event_loop.Run();
}
