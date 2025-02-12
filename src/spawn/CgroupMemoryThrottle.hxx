// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "spawn/CgroupWatch.hxx"
#include "event/CoarseTimerEvent.hxx"

/**
 * Wraps #CgroupMemoryWatch and adds a timer that checks whether we
 * have fallen below the configured limit.
 */
class CgroupMemoryThrottle {
	const BoundMethod<void() noexcept> callback;

	/**
	 * The configured memory limit [bytes].  Zero if none is
	 * configured.
	 */
	const uint_least64_t limit;

	CgroupMemoryWatch watch;

	/**
	 * This timer repeats the memory pressure check periodically
	 * after pressure was once reported until we're below the
	 * threshold.
	 */
	CoarseTimerEvent repeat_timer;

public:
	CgroupMemoryThrottle(EventLoop &event_loop,
			     FileDescriptor group_fd,
			     BoundMethod<void() noexcept> _callback,
			     uint_least64_t _limit);

	auto &GetEventLoop() const noexcept {
		return watch.GetEventLoop();
	}

private:
	void OnMemoryWarning(uint_least64_t memory_usage) noexcept;
	void OnRepeatTimer() noexcept;
};
