// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <chrono>
#include <cstdint>

/**
 * Metrics for #CgroupMemoryThrottle and #CgroupPidsThrottle.
 */
struct CgroupPressureStats {
	/**
	 * How many spawn requests were throttled?
	 */
	uint_least64_t n_throttled = 0;

	/**
	 * How many throttled spawn requests were canceled?
	 */
	uint_least64_t n_canceled = 0;

	/**
	 * The total duration of all throttled spawn requests
	 * (including the ones that were canceled).
	 */
	std::chrono::steady_clock::duration total_throttle_duration{};
};
