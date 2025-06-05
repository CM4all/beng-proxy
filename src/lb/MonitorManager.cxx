// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "MonitorManager.hxx"
#include "MonitorStock.hxx"

LbMonitorManager::LbMonitorManager(EventLoop &_event_loop,
				   FailureManager &_failure_manager) noexcept
	:event_loop(_event_loop), failure_manager(_failure_manager)
{
}

LbMonitorManager::~LbMonitorManager() noexcept
{
}

void
LbMonitorManager::clear() noexcept
{
	monitors.clear();
}

LbMonitorStock &
LbMonitorManager::operator[](const LbMonitorConfig &monitor_config) noexcept
{
	return monitors
		.emplace(std::piecewise_construct,
			 std::forward_as_tuple(&monitor_config),
			 std::forward_as_tuple(event_loop,
					       failure_manager,
					       monitor_config))
		.first->second;
}
