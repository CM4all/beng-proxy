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
	 watch(event_loop, group_fd, BIND_THIS_METHOD(OnMemoryWarning)),
	 repeat_timer(event_loop, BIND_THIS_METHOD(OnRepeatTimer)) {}

inline void
CgroupMemoryThrottle:: OnMemoryWarning(uint_least64_t usage) noexcept
{
	if (limit > 0 && usage < limit / 16 * 15)
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

	try {
		const auto usage = watch.GetMemoryUsage();
		if (usage < limit * 15 / 16)
			return;

		/* repeat until we have a safe margin below the
		   configured memory limit to avoid too much kernel
		   shrinker contention */

		fmt::print(stderr, "Spawner memory warning (repeat): {} of {} bytes used\n",
			   usage, limit);
	} catch (...) {
		PrintException(std::current_exception());
		return;
	}

	callback();

	repeat_timer.Schedule(std::chrono::seconds{2});
}
