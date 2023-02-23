// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "StickyHash.hxx"
#include "event/Chrono.hxx"

class AllocatorPtr;
class BalancerMap;
class FailureManager;
struct AddressList;
class EventLoop;
class ConnectSocketHandler;
class CancellablePointer;
class SocketAddress;

/**
 * Open a connection to any address in the specified address list.
 * This is done in a round-robin fashion, ignoring hosts that are
 * known to be down.
 *
 * @param timeout the connect timeout for each attempt [seconds]
 */
void
client_balancer_connect(EventLoop &event_loop, AllocatorPtr alloc,
			BalancerMap &balancer,
			FailureManager &failure_manager,
			bool ip_transparent,
			SocketAddress bind_address,
			sticky_hash_t sticky_hash,
			const AddressList &address_list,
			Event::Duration timeout,
			ConnectSocketHandler &handler,
			CancellablePointer &cancel_ptr);
