/*
 * Socket address utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address.h"

#include <socket/address.h>

const char *
address_to_string(pool_t pool, const struct sockaddr *addr, size_t addrlen)
{
    bool success;
    char host[512];

    success = socket_address_to_string(host, sizeof(host), addr, addrlen);
    if (!success)
        return NULL;

    return p_strdup(pool, host);
}
