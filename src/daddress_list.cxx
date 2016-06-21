/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_list.hxx"
#include "shm/dpool.hxx"

AddressList::AddressList(struct dpool &pool, const AddressList &src)
    :sticky_mode(src.sticky_mode)
{
    addresses.clear();

    for (const auto &i : src)
        Add(pool, i);
}

bool
AddressList::Add(struct dpool &pool, const SocketAddress address)
{
    if (addresses.full())
        return false;

    const struct sockaddr *new_address = (const struct sockaddr *)
        d_memdup(pool, address.GetAddress(), address.GetSize());
    addresses.push_back({new_address, address.GetSize()});
    return true;
}
