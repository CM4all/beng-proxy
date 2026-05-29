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
	 */
	virtual void OnCgroupPressure() noexcept = 0;
};
