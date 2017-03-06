/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "RConnectSocket.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/SocketDescriptor.hxx"
#include "system/Error.hxx"
#include "io/FileDescriptor.hxx"

SocketDescriptor
ResolveConnectSocket(const char *host_and_port, int default_port,
                     const struct addrinfo &hints)
{
    const auto ai = Resolve(host_and_port, default_port, &hints);

    SocketDescriptor s;
    if (!s.Create(ai->ai_family, ai->ai_socktype, ai->ai_protocol))
        throw MakeErrno("Failed to create socket");

    if (!s.Connect(ai.front())) {
        if (errno != EINPROGRESS)
            throw MakeErrno("Failed to connect");

        int w = FileDescriptor(s.Get()).WaitWritable(60000);
        if (w < 0)
            throw MakeErrno("Connect wait error");
        else if (w == 0)
            throw std::runtime_error("Connect timeout");

        int err = s.GetError();
        if (err != 0)
            throw MakeErrno(err, "Failed to connect");
    }

    return s;
}
