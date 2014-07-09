/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_sticky.h"
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
socket_address_sticky(const struct sockaddr *address)
{
    if (address == NULL)
        return 0;

    switch (address->sa_family) {
    case AF_INET:
        return ipv4_sticky((const struct sockaddr_in *)address);

    case AF_INET6:
        return ipv6_sticky((const struct sockaddr_in6 *)address);

    default:
        return 0;
    }
}
