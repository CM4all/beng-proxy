// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class Lease;
class FilteredSocket;
class SocketAddress;
class ReferencedFailureInfo;

class FilteredSocketBalancerHandler {
public:
	virtual void OnFilteredSocketReady(Lease &lease,
					   FilteredSocket &socket,
					   SocketAddress address,
					   const char *name,
					   ReferencedFailureInfo &failure) noexcept = 0;
	virtual void OnFilteredSocketError(std::exception_ptr e) noexcept = 0;
};
