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

#include "TempListener.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"

#include <sys/stat.h>

static void
make_child_socket_path(struct sockaddr_un *address)
{
	address->sun_family = AF_LOCAL;

	strcpy(address->sun_path, "/tmp/cm4all-beng-proxy-socket-XXXXXX");
	if (*mktemp(address->sun_path) == 0)
		throw MakeErrno("mktemp() failed");
}

TempListener::~TempListener() noexcept
{
	if (IsDefined())
		unlink(address.sun_path);
}

UniqueSocketDescriptor
TempListener::Create(int socket_type, int backlog)
{
	make_child_socket_path(&address);

	unlink(address.sun_path);

	UniqueSocketDescriptor fd;
	if (!fd.Create(AF_LOCAL, socket_type, 0))
		throw MakeErrno("failed to create local socket");

	/* allow only beng-proxy to connect to it */
	fchmod(fd.Get(), 0600);

	if (!fd.Bind(GetAddress()))
		throw MakeErrno("failed to bind local socket");

	if (!fd.Listen(backlog))
		throw MakeErrno("failed to listen on local socket");

	return fd;
}

UniqueSocketDescriptor
TempListener::Connect() const
{
	UniqueSocketDescriptor fd;
	if (!fd.CreateNonBlock(AF_LOCAL, SOCK_STREAM, 0))
		throw MakeErrno("Failed to create socket");

	if (!fd.Connect(GetAddress())) {
		int e = errno;
		fd.Close();
		throw MakeErrno(e, "Failed to connect");
	}

	return fd;
}
