/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "RConnectSocket.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"

UniqueSocketDescriptor
ResolveConnectSocket(const char *host_and_port, int default_port,
                     const struct addrinfo &hints)
{
    const auto ail = Resolve(host_and_port, default_port, &hints);
    const auto &ai = ail.front();

    UniqueSocketDescriptor s;
    if (!s.CreateNonBlock(ai.GetFamily(), ai.GetType(), ai.GetProtocol()))
        throw MakeErrno("Failed to create socket");

    if (!s.Connect(ai)) {
        if (errno != EINPROGRESS)
            throw MakeErrno("Failed to connect");

        int w = s.WaitWritable(60000);
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
