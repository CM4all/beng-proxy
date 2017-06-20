/*
 * Create a listener socket for a child process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_socket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"

#include <sys/stat.h>

static void
make_child_socket_path(struct sockaddr_un *address)
{
    address->sun_family = AF_UNIX;

    strcpy(address->sun_path, "/tmp/cm4all-beng-proxy-socket-XXXXXX");
    if (*mktemp(address->sun_path) == 0)
        throw MakeErrno("mktemp() failed");
}

UniqueSocketDescriptor
ChildSocket::Create(int socket_type)
{
    make_child_socket_path(&address);

    unlink(address.sun_path);

    UniqueSocketDescriptor fd;
    if (!fd.Create(AF_LOCAL, socket_type, 0))
        throw MakeErrno("failed to create local socket");

    if (!fd.Bind(GetAddress()))
        throw MakeErrno("failed to bind local socket");

    /* allow only beng-proxy to connect to it */
    chmod(address.sun_path, 0600);

    if (!fd.Listen(8))
        throw MakeErrno("failed to listen on local socket");

    return fd;
}

void
ChildSocket::Unlink()
{
    unlink(address.sun_path);
}

UniqueSocketDescriptor
ChildSocket::Connect() const
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
