/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_list.hxx"
#include "pool.hxx"

#include <socket/address.h>

#include <assert.h>
#include <string.h>

void
AddressList::CopyFrom(struct pool *pool, const AddressList &src)
{
    Init();
    sticky_mode = src.sticky_mode;

    for (const auto &i : src)
        Add(pool, i);
}

bool
AddressList::Add(struct pool *pool, const SocketAddress address)
{
    if (addresses.full())
        return false;

    const struct sockaddr *new_address = (const struct sockaddr *)
        p_memdup(pool, address.GetAddress(), address.GetSize());
    addresses.push_back({new_address, address.GetSize()});
    return true;
}

const SocketAddress *
AddressList::GetFirst() const
{
    if (addresses.empty())
        return nullptr;

    return &addresses.front();
}

const char *
AddressList::GetKey() const
{
    static char buffer[2048];
    size_t length = 0;
    bool success;

    for (const auto &i : *this) {
        if (length > 0 && length < sizeof(buffer) - 1)
            buffer[length++] = ' ';

        success = socket_address_to_string(buffer + length,
                                           sizeof(buffer) - length,
                                           i.GetAddress(), i.GetSize());
        if (success)
            length += strlen(buffer + length);
    }

    buffer[length] = 0;

    return buffer;
}
