/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Parser.hxx"
#include "AllocatedSocketAddress.hxx"
#include "Error.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"

#include <socket/resolver.h>

#include <assert.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>

#ifndef __linux
static constexpr Domain resolver_domain("resolver");
#endif

AllocatedSocketAddress
ParseSocketAddress(const char *p, int default_port, bool passive, Error &error)
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
        error.Set(resolver_domain, "Abstract sockets supported only on Linux");
        return AllocatedSocketAddress::Null();
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
        error.Format(netdb_domain, result,
                     "Failed to resolve '%s': %s",
                     p, gai_strerror(result));
        return AllocatedSocketAddress::Null();
    }

    AllocatedSocketAddress address(SocketAddress(ai->ai_addr, ai->ai_addrlen));
    freeaddrinfo(ai);

    return address;
}
