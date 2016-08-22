/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Parser.hxx"
#include "AllocatedSocketAddress.hxx"

#include <socket/resolver.h>

#include <stdexcept>

#include <netdb.h>
#include <stdio.h>

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

    struct addrinfo *ai;
    int result = socket_resolve_host_port(p, default_port,
                                          passive ? &passive_hints : &hints,
                                          &ai);
    if (result != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Failed to resolve '%s': %s",
                 p, gai_strerror(result));
        throw std::runtime_error(msg);
    }

    AllocatedSocketAddress address(SocketAddress(ai->ai_addr, ai->ai_addrlen));
    freeaddrinfo(ai);

    return address;
}
