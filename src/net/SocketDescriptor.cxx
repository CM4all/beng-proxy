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

gcc_pure
static bool
IsV6Any(SocketAddress address)
{
    return address.GetFamily() == AF_INET6 &&
        memcmp(&((const struct sockaddr_in6 *)(const void *)address.GetAddress())->sin6_addr,
               &in6addr_any, sizeof(in6addr_any)) == 0;
}

bool
SocketDescriptor::CreateListen(int family, int socktype, int protocol,
                               const SocketAddress &address, Error &error)
{
    if (!Create(family, socktype, protocol, error))
        return false;

    const int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&reuse, sizeof(reuse)) < 0) {
        error.SetErrno("Failed to set SO_REUSEADDR");
        Close();
        return false;
    }

    if (IsV6Any(address))
        SetV6Only(false);

    if (!Bind(address)) {
        error.SetErrno("Failed to bind");
        Close();
        return false;
    }

#ifdef __linux
    /* enable TCP Fast Open (requires Linux 3.7) */

#ifndef TCP_FASTOPEN
#define TCP_FASTOPEN 23
#endif

    if ((family == AF_INET || family == AF_INET6) &&
        socktype == SOCK_STREAM) {
        int qlen = 16;
        setsockopt(fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
    }
#endif

    if (listen(fd, 64) < 0) {
        error.SetErrno("Failed to listen");
        Close();
        return false;
    }

    setsockopt(fd, SOL_SOCKET, SO_PASSCRED,
               (const char *)&reuse, sizeof(reuse));

    return true;
}

bool
SocketDescriptor::Bind(SocketAddress address)
{
    assert(IsDefined());

    return bind(fd, address.GetAddress(), address.GetSize()) == 0;
}

bool
SocketDescriptor::SetTcpDeferAccept(const int &seconds)
{
    assert(IsDefined());

    return setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT,
                      &seconds, sizeof(seconds)) == 0;
}

bool
SocketDescriptor::SetV6Only(bool _value)
{
    assert(IsDefined());

    int value = _value;

    return setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                      &value, sizeof(value)) == 0;
}

bool
SocketDescriptor::SetBindToDevice(const char *name)
{
    assert(IsDefined());

    return setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
                      name, strlen(name)) == 0;
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

