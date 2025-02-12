// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CgroupMemoryThrottle.hxx"
#include "util/PrintException.hxx"

#include <fmt/core.h>

#include <cassert>

CgroupMemoryThrottle::CgroupMemoryThrottle(EventLoop &event_loop,
					   FileDescriptor group_fd,
					   BoundMethod<void() noexcept> _callback,
					   uint_least64_t _limit)
	:callback(_callback),
	 limit(_limit),
	 pressure_threshold(limit / 16 * 15),
	 watch(event_loop, group_fd, BIND_THIS_METHOD(OnMemoryWarning)),
	 repeat_timer(event_loop, BIND_THIS_METHOD(OnRepeatTimer)) {}

uint_least64_t
CgroupMemoryThrottle::IsUnderPressure() const noexcept
{
	assert(limit > 0);

	try {
		const auto usage = watch.GetMemoryUsage();
		return usage >= pressure_threshold ? usage : 0;
	} catch (...) {
		PrintException(std::current_exception());
		return 0;
	}
}

inline void
CgroupMemoryThrottle::OnMemoryWarning(uint_least64_t usage) noexcept
{
	if (limit > 0 && usage < pressure_threshold)
		/* false alarm - we're well below the configured
		   limit */
		return;

	fmt::print(stderr, "Spawner memory warning: {} of {} bytes used\n",
		   usage, limit);

	callback();

	if (limit > 0)
		repeat_timer.ScheduleEarlier(std::chrono::seconds{2});
}

inline void
CgroupMemoryThrottle::OnRepeatTimer() noexcept
{
	assert(limit > 0);

	const uint_least64_t usage = IsUnderPressure();
	if (usage == 0)
		return;

	/* repeat until we have a safe margin below the configured
	   memory limit to avoid too much kernel shrinker
	   contention */

	fmt::print(stderr, "Spawner memory warning (repeat): {} of {} bytes used\n",
		   usage, limit);

	callback();

	repeat_timer.Schedule(std::chrono::seconds{2});
}
