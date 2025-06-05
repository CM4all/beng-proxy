// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "cluster/BalancerMap.hxx"
#include "event/Chrono.hxx"

struct AddressList;
class EventLoop;
class FilteredSocketBalancerHandler;
class CancellablePointer;
class SocketAddress;
class SocketFilterParams;
class FilteredSocketStock;
class StopwatchPtr;
class AllocatorPtr;
class FailureManager;

/*
 * Wrapper for the #FilteredSocketStock class to support load
 * balancing.
 */
class FilteredSocketBalancer {
	FilteredSocketStock &stock;

	FailureManager &failure_manager;

	BalancerMap balancer;

public:
	FilteredSocketBalancer(FilteredSocketStock &_stock,
			       FailureManager &_failure_manager) noexcept
		:stock(_stock), failure_manager(_failure_manager) {}

	[[gnu::pure]]
	EventLoop &GetEventLoop() noexcept;

	auto &GetStock() const noexcept {
		return stock;
	}

	FailureManager &GetFailureManager() {
		return failure_manager;
	}

	class Request;

	/**
	 * @param fairness_hash if non-zero, then two consecutive
	 * requests with the same value are avoided (for fair
	 * scheduling)
	 *
	 * @param sticky_hash a portion of the session id that is used to
	 * select the worker; 0 means disable stickiness
	 * @param timeout the connect timeout for each attempt [seconds]
	 */
	void Get(AllocatorPtr alloc,
		 const StopwatchPtr &parent_stopwatch,
		 uint_fast64_t fairness_hash,
		 bool ip_transparent,
		 SocketAddress bind_address,
		 sticky_hash_t sticky_hash,
		 const AddressList &address_list,
		 Event::Duration timeout,
		 const SocketFilterParams *filter_params,
		 FilteredSocketBalancerHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;
};
