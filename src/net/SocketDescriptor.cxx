/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SocketDescriptor.hxx"
#include "SocketAddress.hxx"
#include "StaticSocketAddress.hxx"
#include "util/Error.hxx"

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>

SocketDescriptor::~SocketDescriptor()
{
    if (IsDefined())
        Close();
}

void
SocketDescriptor::Close()
{
    assert(IsDefined());

    close(fd);
    fd = -1;
}

bool
SocketDescriptor::Create(int domain, int type, int protocol)
{
    assert(!IsDefined());

    type |= SOCK_CLOEXEC|SOCK_NONBLOCK;
    fd = socket(domain, type, protocol);
    return fd >= 0;
}

bool
SocketDescriptor::Create(int domain, int type, int protocol, Error &error)
{
    assert(!IsDefined());

    type |= SOCK_CLOEXEC|SOCK_NONBLOCK;
    fd = socket(domain, type, protocol);
    if (fd < 0) {
        error.SetErrno("Failed to create socket");
        return false;
    }

    return true;
}

bool
SocketDescriptor::CreateListen(int family, int socktype, int protocol,
                               const SocketAddress &address,
                               bool reuse_port,
                               Error &error)
{
    if (!Create(family, socktype, protocol, error))
        return false;

    if (!SetBoolOption(SOL_SOCKET, SO_REUSEADDR, true)) {
        error.SetErrno("Failed to set SO_REUSEADDR");
        Close();
        return false;
    }

    if (reuse_port && !SetBoolOption(SOL_SOCKET, SO_REUSEPORT, true)) {
        error.SetErrno("Failed to set SO_REUSEPORT");
        Close();
        return false;
    }

    if (address.IsV6Any())
        SetV6Only(false);

    if (!Bind(address)) {
        error.SetErrno("Failed to bind");
        Close();
        return false;
    }

    if ((family == AF_INET || family == AF_INET6) &&
        socktype == SOCK_STREAM)
        SetTcpFastOpen();

    if (listen(fd, 64) < 0) {
        error.SetErrno("Failed to listen");
        Close();
        return false;
    }

    SetBoolOption(SOL_SOCKET, SO_PASSCRED, true);

    return true;
}

bool
SocketDescriptor::Bind(SocketAddress address)
{
    assert(IsDefined());

    return bind(fd, address.GetAddress(), address.GetSize()) == 0;
}

bool
SocketDescriptor::SetOption(int level, int name,
                            const void *value, size_t size)
{
    assert(IsDefined());

    return setsockopt(fd, level, name, value, size) == 0;
}

bool
SocketDescriptor::SetReuseAddress(bool value)
{
    return SetBoolOption(SOL_SOCKET, SO_REUSEADDR, value);
}

bool
SocketDescriptor::SetReusePort(bool value)
{
    return SetBoolOption(SOL_SOCKET, SO_REUSEPORT, value);
}

bool
SocketDescriptor::SetTcpDeferAccept(const int &seconds)
{
    return SetOption(IPPROTO_TCP, TCP_DEFER_ACCEPT, &seconds, sizeof(seconds));
}

bool
SocketDescriptor::SetV6Only(bool value)
{
    return SetBoolOption(IPPROTO_IPV6, IPV6_V6ONLY, value);
}

bool
SocketDescriptor::SetBindToDevice(const char *name)
{
    return SetOption(SOL_SOCKET, SO_BINDTODEVICE, name, strlen(name));
}

bool
SocketDescriptor::SetTcpFastOpen(int qlen)
{
    return SetOption(SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
}

SocketDescriptor
SocketDescriptor::Accept(StaticSocketAddress &address, Error &error) const
{
    assert(IsDefined());

    address.size = address.GetCapacity();
    int result = accept4(fd, address, &address.size,
                         SOCK_CLOEXEC|SOCK_NONBLOCK);
    if (result < 0) {
        error.SetErrno("Failed to accept connection");
        return SocketDescriptor();
    }

    return SocketDescriptor(result);
}

bool
SocketDescriptor::Connect(const SocketAddress address)
{
    assert(IsDefined());

    return connect(fd, address.GetAddress(), address.GetSize()) == 0;
}

int
SocketDescriptor::GetError()
{
    assert(IsDefined());

    int s_err = 0;
    socklen_t s_err_size = sizeof(s_err);
    return getsockopt(fd, SOL_SOCKET, SO_ERROR,
                      (char *)&s_err, &s_err_size) == 0
        ? s_err
        : errno;
}

StaticSocketAddress
SocketDescriptor::GetLocalAddress() const
{
    assert(IsDefined());

    StaticSocketAddress result;
    result.size = result.GetCapacity();
    if (getsockname(fd, result, &result.size) < 0)
        result.Clear();

    return result;
}

