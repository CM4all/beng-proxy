/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ServerSocket.hxx"
#include "SocketDescriptor.hxx"
#include "SocketAddress.hxx"
#include "StaticSocketAddress.hxx"
#include "AllocatedSocketAddress.hxx"
#include "system/fd_util.h"
#include "event/Callback.hxx"
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

void
ServerSocket::EventCallback(gcc_unused short events)
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

bool
ServerSocket::Listen(int family, int socktype, int protocol,
                     SocketAddress address,
                     bool reuse_port,
                     const char *bind_to_device,
                     Error &error)
{
    if (address.GetFamily() == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)address.GetAddress();
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);
    }

    if (!fd.Create(family, socktype, protocol, error))
        return false;

    if (!fd.SetBoolOption(SOL_SOCKET, SO_REUSEADDR, true)) {
        error.SetErrno("Failed to set SO_REUSEADDR");
        return false;
    }

    if (reuse_port && !fd.SetBoolOption(SOL_SOCKET, SO_REUSEPORT, true)) {
        error.SetErrno("Failed to set SO_REUSEPORT");
        return false;
    }

    if (address.IsV6Any())
        fd.SetV6Only(false);

    if (bind_to_device != nullptr && !fd.SetBindToDevice(bind_to_device)) {
        error.SetErrno("Failed to set SO_BINDTODEVICE");
        return false;
    }

    if (!fd.Bind(address)) {
        error.SetErrno("Failed to bind");
        return false;
    }

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

    if (listen(fd.Get(), 64) < 0) {
        error.SetErrno("Failed to listen");
        return false;
    }

    event.Set(fd.Get(), EV_READ|EV_PERSIST);
    AddEvent();
    return true;
}

bool
ServerSocket::ListenTCP(unsigned port, Error &error)
{
    return ListenTCP6(port, IgnoreError()) || ListenTCP4(port, error);
}

bool
ServerSocket::ListenTCP4(unsigned port, Error &error)
{
    assert(port > 0);

    struct sockaddr_in sa4;

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = htons(port);

    return Listen(PF_INET, SOCK_STREAM, 0,
                  SocketAddress((const struct sockaddr *)&sa4, sizeof(sa4)),
                  false, nullptr, error);
}

bool
ServerSocket::ListenTCP6(unsigned port, Error &error)
{
    assert(port > 0);

    struct sockaddr_in6 sa6;

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = htons(port);

    return Listen(PF_INET6, SOCK_STREAM, 0,
                  SocketAddress((const struct sockaddr *)&sa6, sizeof(sa6)),
                  false, nullptr, error);
}

bool
ServerSocket::ListenPath(const char *path, Error &error)
{
    AllocatedSocketAddress address;
    address.SetLocal(path);

    return Listen(AF_LOCAL, SOCK_STREAM, 0, address, false, nullptr, error);
}

StaticSocketAddress
ServerSocket::GetLocalAddress() const
{
    return fd.GetLocalAddress();
}

ServerSocket::~ServerSocket()
{
    if (fd.IsDefined())
        event.Delete();
}
