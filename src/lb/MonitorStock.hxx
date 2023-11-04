// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <map>
#include <string>

struct LbNodeConfig;
struct LbMonitorConfig;
struct LbMonitorClass;
class LbMonitorRef;
class LbMonitorController;
class EventLoop;
class FailureManager;
class SocketAddress;

/**
 * A manager for LbMonitorController instances created with one
 * #LbMonitorConfig for different nodes.
 */
class LbMonitorStock {
	EventLoop &event_loop;
	FailureManager &failure_manager;
	const LbMonitorConfig &config;
	const LbMonitorClass &class_;

	std::map<std::string, LbMonitorController> map;

public:
	LbMonitorStock(EventLoop &_event_loop,
		       FailureManager &_failure_manager,
		       const LbMonitorConfig &_config);
	~LbMonitorStock();

	LbMonitorStock(const LbMonitorStock &) = delete;
	LbMonitorStock &operator=(const LbMonitorStock &) = delete;

	LbMonitorRef Add(std::string_view node_name, SocketAddress address);

	LbMonitorRef Add(const LbNodeConfig &node, unsigned port);

	void Remove(LbMonitorController &m) noexcept;
};
