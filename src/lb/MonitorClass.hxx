// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class EventLoop;
class SocketAddress;
class CancellablePointer;
struct LbMonitorConfig;

struct LbMonitorClass {
	void (*run)(EventLoop &event_loop,
		    const LbMonitorConfig &config,
		    SocketAddress address,
		    LbMonitorHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept;
};
