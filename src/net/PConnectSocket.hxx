// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/net/ConnectSocket.hxx"

class AllocatorPtr;
class EventLoop;
class SocketAddress;
class UniqueSocketDescriptor;
class CancellablePointer;
class StopwatchPtr;

/**
 * TCP client socket with asynchronous connect.
 *
 * @param ip_transparent enable the IP_TRANSPARENT option?
 * @param timeout the connect timeout in seconds
 */
void
client_socket_new(EventLoop &event_loop, AllocatorPtr alloc,
		  StopwatchPtr stopwatch,
		  int domain, int type, int protocol,
		  bool ip_transparent,
		  const SocketAddress bind_address,
		  const SocketAddress address,
		  Event::Duration timeout,
		  ConnectSocketHandler &handler,
		  CancellablePointer &cancel_ptr);
