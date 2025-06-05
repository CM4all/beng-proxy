// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <utility> // for std::pair

class UniqueSocketDescriptor;
class SocketAddress;

/**
 * Throws on error.
 *
 * @param ip_transparent enable the IP_TRANSPARENT option?
 *
 * @return the socket and a boolean specifying whether the connect has
 * completed (false means EAGAIN)
 */
std::pair<UniqueSocketDescriptor, bool>
CreateConnectSocketNonBlock(int domain, int type, int protocol,
			    bool ip_transparent,
			    SocketAddress bind_address,
			    SocketAddress address);
