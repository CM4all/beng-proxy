/*
 * Create a listener socket for a child process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_socket.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "net/SocketDescriptor.hxx"
#include "gerrno.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static bool
make_child_socket_path(struct sockaddr_un *address)
{
    address->sun_family = AF_UNIX;

    strcpy(address->sun_path, "/tmp/cm4all-beng-proxy-socket-XXXXXX");
    return *mktemp(address->sun_path) != 0;
}

UniqueFileDescriptor
ChildSocket::Create(int socket_type, GError **error_r)
{
    if (!make_child_socket_path(&address)) {
        set_error_errno_msg(error_r, "mktemp() failed");
        return UniqueFileDescriptor();
    }

    unlink(address.sun_path);

    const int fd = socket(PF_UNIX, socket_type, 0);
    if (fd < 0) {
        set_error_errno_msg(error_r, "failed to create local socket");
        return UniqueFileDescriptor();
    }

    UniqueFileDescriptor ufd((FileDescriptor(fd)));

    if (bind(fd, GetAddress().GetAddress(), GetAddress().GetSize()) < 0) {
        set_error_errno_msg(error_r, "failed to bind local socket");
        return UniqueFileDescriptor();
    }

    /* allow only beng-proxy to connect to it */
    chmod(address.sun_path, 0600);

    if (listen(fd, 8) < 0) {
        set_error_errno_msg(error_r, "failed to listen on local socket");
        return UniqueFileDescriptor();
    }

    return ufd;
}

void
ChildSocket::Unlink()
{
    unlink(address.sun_path);
}

int
ChildSocket::Connect(GError **error_r) const
{
    SocketDescriptor fd;
    if (!fd.CreateNonBlock(AF_LOCAL, SOCK_STREAM, 0)) {
        set_error_errno(error_r);
        return -1;
    }

    if (!fd.Connect(GetAddress())) {
        set_error_errno_msg(error_r, "connect failed");
        fd.Close();
        return -1;
    }

    return fd.Get();
}
