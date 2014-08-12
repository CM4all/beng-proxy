/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ServerSocket.hxx"
#include "SocketDescriptor.hxx"
#include "SocketAddress.hxx"
#include "StaticSocketAddress.hxx"
#include "fd_util.h"
#include "pool.hxx"
#include "util/Error.hxx"

#include <socket/util.h>
#include <socket/address.h>

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

static bool
IsTCP(SocketAddress address)
{
    return address.GetFamily() == AF_INET || address.GetFamily() == AF_INET6;
}

inline void
ServerSocket::Callback()
{
    StaticSocketAddress remote_address;
    Error error;
    auto remote_fd = fd.Accept(remote_address, error);
    if (!remote_fd.IsDefined()) {
        if (!error.IsDomain(errno_domain) ||
            (error.GetCode() != EAGAIN && error.GetCode() != EWOULDBLOCK))
            OnAcceptError(std::move(error));

        return;
    }

    if (IsTCP(remote_address) &&
        !socket_set_nodelay(remote_fd.Get(), true)) {
        error.SetErrno("setsockopt(TCP_NODELAY) failed");
        OnAcceptError(std::move(error));
        return;
    }

    OnAccept(std::move(remote_fd), remote_address);
}

void
ServerSocket::Callback(gcc_unused int fd, gcc_unused short event, void *ctx)
{
    ServerSocket &ss = *(ServerSocket *)ctx;

    ss.Callback();

    pool_commit();
}

bool
ServerSocket::Listen(int family, int socktype, int protocol,
                     SocketAddress address,
                     Error &error)
{
    if (address.GetFamily() == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)(const struct sockaddr *)address;
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);
    }

    if (!fd.CreateListen(family, socktype, protocol, address, error))
        return nullptr;

    event_set(&event, fd.Get(), EV_READ|EV_PERSIST, Callback, this);
    AddEvent();
    return true;
}

bool
ServerSocket::ListenTCP(unsigned port, Error &error)
{
    assert(port > 0);

    struct sockaddr_in6 sa6;
    struct sockaddr_in sa4;

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = htons(port);

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = htons(port);

    return Listen(PF_INET6, SOCK_STREAM, 0,
                  SocketAddress((const struct sockaddr *)&sa6, sizeof(sa6)),
                  IgnoreError()) ||
        Listen(PF_INET, SOCK_STREAM, 0,
               SocketAddress((const struct sockaddr *)&sa4, sizeof(sa4)),
               error);
}

bool
ServerSocket::ListenPath(const char *path, Error &error)
{
    unlink(path);

    StaticSocketAddress address;
    address.SetLocal(path);

    return Listen(AF_LOCAL, SOCK_STREAM, 0, address, error);
}

ServerSocket::~ServerSocket()
{
    event_del(&event);
}
