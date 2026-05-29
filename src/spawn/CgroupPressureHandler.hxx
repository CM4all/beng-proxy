// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/**
 * Handler for #CgroupMemoryThrottle and #CgroupPidsThrottle.
 */
class CgroupPressureHandler {
public:
	/**
	 * There is pressure on some resource occupied by our spawned
	 * processes.  The handler shall do something to reduce
	 * resource usage (e.g. kill some processes or throttle
	 * spawning more processes).
	 *
	 * @param repeat how many times this call has been repeated;
	 * the first call means zero, and repeated calls increment
	 * this number; once we fall below the threshold, the counter
	 * is reset to zero
	 */
	virtual void OnCgroupPressure(unsigned repeat) noexcept = 0;
};
