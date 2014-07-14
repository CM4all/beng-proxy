/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SocketDescriptor.hxx"
#include "SocketAddress.hxx"
#include "StaticSocketAddress.hxx"
#include "util/Error.hxx"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/tcp.h>

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
                               const SocketAddress &address, Error &error)
{
    if (!Create(family, socktype, protocol, error))
        return false;

    const int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&reuse, sizeof(reuse)) < 0) {
        error.SetErrno("Failed to set SO_REUSEADDR");
        Close();
        return -1;
    }

    if (bind(fd, address, address.GetSize()) < 0) {
        error.SetErrno("Failed to bind");
        Close();
        return -1;
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
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_PASSCRED,
               (const char *)&reuse, sizeof(reuse));

    return fd;
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

