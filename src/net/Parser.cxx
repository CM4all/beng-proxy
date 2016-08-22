/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Parser.hxx"
#include "AddressInfo.hxx"
#include "Resolver.hxx"
#include "AllocatedSocketAddress.hxx"

#include <stdexcept>

#include <netdb.h>

AllocatedSocketAddress
ParseSocketAddress(const char *p, int default_port, bool passive)
{
    if (*p == '/') {
        AllocatedSocketAddress address;
        address.SetLocal(p);
        return address;
    }

    if (*p == '@') {
#ifdef __linux
        /* abstract unix domain socket */

        AllocatedSocketAddress address;
        address.SetLocal(p);
        return address;
#else
        /* Linux specific feature */
        throw std::runtime_error("Abstract sockets supported only on Linux");
#endif
    }

    static const struct addrinfo hints = {
        .ai_flags = AI_NUMERICHOST,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    static const struct addrinfo passive_hints = {
        .ai_flags = AI_NUMERICHOST|AI_PASSIVE,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };

    const auto ai = Resolve(p, default_port,
                            passive ? &passive_hints : &hints);
    return AllocatedSocketAddress(ai.front());
}
