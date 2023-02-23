// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TempListener.hxx"
#include "net/ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"

#include <stdlib.h>
#include <sys/stat.h>

static void
make_child_socket_path(struct sockaddr_un *address)
{
	const char *runtime_directory = getenv("RUNTIME_DIRECTORY");
	if (runtime_directory != nullptr)
		sprintf(address->sun_path, "%s/temp-socket-XXXXXX",
			runtime_directory);
	else
		strcpy(address->sun_path, "/tmp/cm4all-beng-proxy-socket-XXXXXX");

	if (*mktemp(address->sun_path) == 0)
		throw MakeErrno("mktemp() failed");

	address->sun_family = AF_LOCAL;
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
	return CreateConnectSocket(GetAddress(), SOCK_STREAM);
}
