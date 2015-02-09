/*
 * Allocating struct SocketAddress from memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PADDRESS_HXX
#define PADDRESS_HXX

#include "pool.hxx"
#include "net/SocketAddress.hxx"

static inline SocketAddress
DupAddress(struct pool &pool, SocketAddress src)
{
    return src.IsNull()
        ? src
        : SocketAddress((const struct sockaddr *)
                        p_memdup(&pool, src.GetAddress(), src.GetSize()),
                        src.GetSize());
}

#endif
