// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ConnectSocketX.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketError.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <cassert>

#include <sys/socket.h>
#include <netinet/in.h>

std::pair<UniqueSocketDescriptor, bool>
CreateConnectSocketNonBlock(int domain, int type, int protocol,
			    bool ip_transparent,
			    SocketAddress bind_address,
			    SocketAddress address)
{
	assert(!address.IsNull());

	UniqueSocketDescriptor fd;
	if (!fd.CreateNonBlock(domain, type, protocol))
		throw MakeSocketError("Failed to create socket");

	if ((domain == PF_INET || domain == PF_INET6) &&
	    type == SOCK_STREAM &&
	    !fd.SetNoDelay())
		throw MakeSocketError("Failed to set TCP_NODELAY");

	if (ip_transparent && !fd.SetBoolOption(SOL_IP, IP_TRANSPARENT, true))
		throw MakeSocketError("Failed to set IP_TRANSPARENT");

	if (!bind_address.IsNull() && bind_address.IsDefined()) {
		if (bind_address.HasPort() && bind_address.GetPort() == 0)
			/* delay port allocation to avoid running out
			   of ports (EADDRINUSE) */
			fd.SetBoolOption(SOL_IP, IP_BIND_ADDRESS_NO_PORT, true);

		if (!fd.Bind(bind_address))
			throw MakeSocketError("Failed to bind socket");
	}

	bool completed = fd.Connect(address);
	if (!completed) {
		const auto e = GetSocketError();
		if (!IsSocketErrorConnectWouldBlock(e))
			throw MakeSocketError(e, "Failed to connect");
	}

	return {std::move(fd), completed};
}
