/*
 * Socket address utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_string.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <socket/address.h>

const char *
address_to_string(struct pool &pool, SocketAddress address)
{
    if (address.IsNull())
        return nullptr;

    bool success;
    char host[512];

    success = socket_address_to_string(host, sizeof(host),
                                       address.GetAddress(), address.GetSize());
    if (!success || *host == 0)
        return nullptr;

    return p_strdup(&pool, host);
}

const char *
address_to_host_string(struct pool &pool, SocketAddress address)
{
    if (address.IsNull())
        return nullptr;

    bool success;
    char host[512];

    success = socket_host_to_string(host, sizeof(host),
                                    address.GetAddress(), address.GetSize());
    if (!success || *host == 0)
        return nullptr;

    return p_strdup(&pool, host);
}
