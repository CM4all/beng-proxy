/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_sticky.hxx"
#include "net/SocketAddress.hxx"
#include "util/djbhash.h"
#include "util/ConstBuffer.hxx"

sticky_hash_t
socket_address_sticky(SocketAddress address)
{
    const auto p = address.GetSteadyPart();
    if (p.IsNull())
        return 0;

    return djb_hash(p.data, p.size);
}
