// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Local.hxx"
#include "event/net/control/Server.hxx"
#include "net/SocketConfig.hxx"

#include <sys/un.h>
#include <stdio.h>

void
LocalControl::OnControlPacket(ControlServer &control_server,
			      BengProxy::ControlCommand command,
			      std::span<const std::byte> payload,
			      std::span<UniqueFileDescriptor> fds,
			      SocketAddress address, int uid)
{
	if (uid < 0 || (uid != 0 && (uid_t)uid != geteuid()))
		/* only root and the beng-proxy user are allowed to send
		   commands to the implicit control channel */
		return;

	handler.OnControlPacket(control_server, command,
				payload, fds, address, uid);
}

void
LocalControl::OnControlError(std::exception_ptr ep) noexcept
{
	handler.OnControlError(ep);
}

void
LocalControl::Open(EventLoop &event_loop)
{
	server.reset();

	struct sockaddr_un sa;
	sa.sun_family = AF_LOCAL;
	sa.sun_path[0] = '\0';
	sprintf(sa.sun_path + 1, "%s%d", prefix, (int)getpid());

	SocketConfig config;
	config.bind_address = SocketAddress((const struct sockaddr *)&sa,
					    SUN_LEN(&sa) + 1 + strlen(sa.sun_path + 1));
	config.pass_cred = true;

	server.reset(new ControlServer(event_loop, *this, config));
}
