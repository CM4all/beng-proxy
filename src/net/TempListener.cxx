// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TempListener.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "io/RuntimeDirectory.hxx"

#include <sys/stat.h>

static const char *
make_child_socket_path(struct sockaddr_un *address)
{
	address->sun_family = AF_LOCAL;

	return MakePrivateRuntimeDirectoryTemp(std::span{address->sun_path},
					       "temp-socket-XXXXXX",
					       "cm4all-beng-proxy-XXXXXX");
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
		throw MakeSocketError("failed to create local socket");

	/* allow only beng-proxy to connect to it */
	fchmod(fd.Get(), 0600);

	if (!fd.Bind(GetAddress()))
		throw MakeSocketError("failed to bind local socket");

	if (!fd.Listen(backlog))
		throw MakeSocketError("failed to listen on local socket");

	return fd;
}

UniqueSocketDescriptor
TempListener::Connect() const
{
	return CreateConnectSocketNonBlock(GetAddress(), SOCK_STREAM);
}
