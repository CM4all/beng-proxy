/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Local.hxx"
#include "Server.hxx"
#include "net/SocketConfig.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <sys/un.h>
#include <stdio.h>

void
LocalControl::OnControlPacket(ControlServer &control_server,
			      BengProxy::ControlCommand command,
			      ConstBuffer<void> payload,
			      WritableBuffer<UniqueFileDescriptor> fds,
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
					    SUN_LEN(&sa) + 1 + strlen(sa.sun_path + 1)),
		config.pass_cred = true;

	server.reset(new ControlServer(event_loop, *this, config));
}
