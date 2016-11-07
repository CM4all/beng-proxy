/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_list.hxx"
#include "net/AddressInfo.hxx"
#include "AllocatorPtr.hxx"

#include <socket/address.h>

#include <string.h>

AddressList::AddressList(ShallowCopy, const AddressInfo &src)
{
    for (const auto &i : src) {
        if (addresses.full())
            break;

        addresses.push_back(i);
    }
}

AddressList::AddressList(AllocatorPtr alloc, const AddressList &src)
    :sticky_mode(src.sticky_mode)
{
    addresses.clear();

    for (const auto &i : src)
        Add(alloc, i);
}

bool
AddressList::Add(AllocatorPtr alloc, const SocketAddress address)
{
    if (addresses.full())
        return false;

    const struct sockaddr *new_address = (const struct sockaddr *)
        alloc.Dup(address.GetAddress(), address.GetSize());
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
