// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <map>

struct LbMonitorConfig;
class LbMonitorStock;
class EventLoop;
class FailureManager;

/**
 * A manager for LbMonitorStock instances.
 */
class LbMonitorManager {
	EventLoop &event_loop;
	FailureManager &failure_manager;

	std::map<const LbMonitorConfig *, LbMonitorStock> monitors;

public:
	LbMonitorManager(EventLoop &_event_loop,
			 FailureManager &_failure_manager) noexcept;

	~LbMonitorManager() noexcept;

	LbMonitorManager(const LbMonitorManager &) = delete;
	LbMonitorManager &operator=(const LbMonitorManager &) = delete;

	void clear() noexcept;

	[[gnu::pure]]
	LbMonitorStock &operator[](const LbMonitorConfig &monitor_config) noexcept;
};
