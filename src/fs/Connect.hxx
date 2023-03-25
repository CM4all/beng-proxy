// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "fs/Ptr.hxx"
#include "event/Chrono.hxx"

#include <exception>
#include <memory>

class FilteredSocket;
class EventLoop;
class StopwatchPtr;
class SocketAddress;
class CancellablePointer;

class ConnectFilteredSocketHandler {
public:
	virtual void OnConnectFilteredSocket(std::unique_ptr<FilteredSocket> socket) noexcept = 0;
	virtual void OnConnectFilteredSocketError(std::exception_ptr e) noexcept = 0;
};

void
ConnectFilteredSocket(EventLoop &event_loop,
		      StopwatchPtr stopwatch,
		      bool ip_transparent,
		      SocketAddress bind_address,
		      SocketAddress address,
		      Event::Duration timeout,
		      SocketFilterFactoryPtr filter_factory,
		      ConnectFilteredSocketHandler &handler,
		      CancellablePointer &cancel_ptr) noexcept;
