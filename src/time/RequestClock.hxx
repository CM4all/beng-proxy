// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <chrono>

/**
 * A class that helps with calculating the duration of a HTTP request
 * being processed by this server, not including the time waiting for
 * the client.
 */
class RequestClock final {
	/**
	 * The time stamp at the start of the request.
	 */
	const std::chrono::steady_clock::time_point start_time;

public:
	explicit constexpr RequestClock(std::chrono::steady_clock::time_point now) noexcept
		:start_time(now) {}

	/**
	 * @param wait_duration the total duration waiting for the
	 * client (either request body data or response body)
	 */
	constexpr std::chrono::steady_clock::duration GetDuration(std::chrono::steady_clock::time_point now,
								  std::chrono::steady_clock::duration wait_duration) const noexcept {
		const auto total_duration = now - start_time;
		if (total_duration.count() < 0)
			/* a CLOCK_MONOTONIC warp - should never
			   happen, but sometimes does */
			return {};

		if (wait_duration.count() <= 0)
			/* probably caused by a CLOCK_MONOTONIC warp,
			   too */
			return total_duration;

		if (wait_duration >= total_duration)
			/* same */
			return {};

		return total_duration - wait_duration;
	}
};
