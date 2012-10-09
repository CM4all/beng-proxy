/*
 * Edit struct sockaddr objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_edit.h"
#include "pool.h"

#include <sys/socket.h>
#include <netinet/in.h>

const struct sockaddr *
sockaddr_set_port(struct pool *pool,
                  const struct sockaddr *address, size_t length,
                  unsigned port)
{
    struct sockaddr_in *sa4;
    struct sockaddr_in6 *sa6;

    switch (address->sa_family) {
    case AF_INET:
        sa4 = p_memdup(pool, address, length);
        sa4->sin_port = htons(port);
        return (const struct sockaddr *)sa4;

    case AF_INET6:
        sa6 = p_memdup(pool, address, length);
        sa6->sin6_port = htons(port);
        return (const struct sockaddr *)sa6;
    }

    return address;
}
