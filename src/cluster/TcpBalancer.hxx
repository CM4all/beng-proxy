// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "StickyHash.hxx"
#include "BalancerMap.hxx"
#include "event/Chrono.hxx"

class AllocatorPtr;
struct AddressList;
class TcpStock;
class StockGetHandler;
class CancellablePointer;
class SocketAddress;
class EventLoop;
class StopwatchPtr;
class FailureManager;

/**
 * Wrapper for #TcpStock to support load balancing.
 */
class TcpBalancer {
	friend class TcpBalancerRequest;

	TcpStock &tcp_stock;

	FailureManager &failure_manager;

	BalancerMap balancer;

public:
	/**
	 * @param tcp_stock the underlying #TcpStock object
	 */
	TcpBalancer(TcpStock &_tcp_stock,
		    FailureManager &_failure_manager) noexcept
		:tcp_stock(_tcp_stock), failure_manager(_failure_manager) {}

	EventLoop &GetEventLoop() noexcept;

	FailureManager &GetFailureManager() {
		return failure_manager;
	}

	/**
	 * @param sticky_hash a portion of the session id that is used to
	 * select the worker; 0 means disable stickiness
	 * @param timeout the connect timeout for each attempt
	 */
	void Get(AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
		 bool ip_transparent,
		 SocketAddress bind_address,
		 sticky_hash_t sticky_hash,
		 const AddressList &address_list,
		 Event::Duration timeout,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr);
};
