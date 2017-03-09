/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ServerSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "system/fd_util.h"
#include "system/Error.hxx"

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

ServerSocket::~ServerSocket()
{
    if (fd.IsDefined())
        event.Delete();
}

static bool
IsTCP(SocketAddress address)
{
    return address.GetFamily() == AF_INET || address.GetFamily() == AF_INET6;
}

void
ServerSocket::Listen(int family, int socktype, int protocol,
                     SocketAddress address,
                     bool reuse_port,
                     const char *bind_to_device)
try {
    if (address.GetFamily() == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)address.GetAddress();
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);
    }

    if (!fd.Create(family, socktype, protocol))
        throw MakeErrno("Failed to create socket");

    if (!fd.SetBoolOption(SOL_SOCKET, SO_REUSEADDR, true))
        throw MakeErrno("Failed to set SO_REUSEADDR");

    if (reuse_port && !fd.SetBoolOption(SOL_SOCKET, SO_REUSEPORT, true))
        throw MakeErrno("Failed to set SO_REUSEPORT");

    if (address.IsV6Any())
        fd.SetV6Only(false);

    if (bind_to_device != nullptr && !fd.SetBindToDevice(bind_to_device))
        throw MakeErrno("Failed to set SO_BINDTODEVICE");

    if (!fd.Bind(address))
        throw MakeErrno("Failed to bind");

    switch (family) {
    case AF_INET:
    case AF_INET6:
        if (socktype == SOCK_STREAM)
            fd.SetTcpFastOpen();
        break;

    case AF_LOCAL:
        fd.SetBoolOption(SOL_SOCKET, SO_PASSCRED, true);
        break;
    }

    if (listen(fd.Get(), 64) < 0)
        throw MakeErrno("Failed to listen");

    event.Set(fd.Get(), EV_READ|EV_PERSIST);
    AddEvent();
} catch (...) {
    if (fd.IsDefined())
        fd.Close();
    throw;
}

void
ServerSocket::ListenTCP(unsigned port)
{
    try {
        ListenTCP6(port);
    } catch (...) {
        ListenTCP4(port);
    }
}

void
ServerSocket::ListenTCP4(unsigned port)
{
    assert(port > 0);

    struct sockaddr_in sa4;

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = htons(port);

    Listen(PF_INET, SOCK_STREAM, 0,
           SocketAddress((const struct sockaddr *)&sa4, sizeof(sa4)),
           false, nullptr);
}

void
ServerSocket::ListenTCP6(unsigned port)
{
    assert(port > 0);

    struct sockaddr_in6 sa6;

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = htons(port);

    Listen(PF_INET6, SOCK_STREAM, 0,
           SocketAddress((const struct sockaddr *)&sa6, sizeof(sa6)),
           false, nullptr);
}

void
ServerSocket::ListenPath(const char *path)
{
    AllocatedSocketAddress address;
    address.SetLocal(path);

    Listen(AF_LOCAL, SOCK_STREAM, 0, address, false, nullptr);
}

StaticSocketAddress
ServerSocket::GetLocalAddress() const
{
    return fd.GetLocalAddress();
}

void
ServerSocket::EventCallback(unsigned)
{
    StaticSocketAddress remote_address;
    auto remote_fd = fd.Accept(remote_address);
    if (!remote_fd.IsDefined()) {
        const int e = errno;
        if (e != EAGAIN && e != EWOULDBLOCK)
            OnAcceptError(std::make_exception_ptr(MakeErrno(e, "Failed to accept connection")));

        return;
    }

    if (IsTCP(remote_address) &&
        !socket_set_nodelay(remote_fd.Get(), true)) {
        OnAcceptError(std::make_exception_ptr(MakeErrno("setsockopt(TCP_NODELAY) failed")));
        return;
    }

    OnAccept(std::move(remote_fd), remote_address);
}
