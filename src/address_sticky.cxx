/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_sticky.hxx"
#include "net/SocketAddress.hxx"
#include "util/djbhash.h"

#include <sys/socket.h>
#include <netinet/in.h>

gcc_pure
static unsigned
ipv4_sticky(const struct sockaddr_in *address)
{
    return djb_hash(&address->sin_addr, sizeof(address->sin_addr));
}

gcc_pure
static unsigned
ipv6_sticky(const struct sockaddr_in6 *address)
{
    return djb_hash(&address->sin6_addr, sizeof(address->sin6_addr));
}

unsigned
socket_address_sticky(SocketAddress address)
{
    if (address.IsNull())
        return 0;

    switch (address.GetFamily()) {
    case AF_INET:
        return ipv4_sticky((const struct sockaddr_in *)address.GetAddress());

    case AF_INET6:
        return ipv6_sticky((const struct sockaddr_in6 *)address.GetAddress());

    default:
        return 0;
    }
}
