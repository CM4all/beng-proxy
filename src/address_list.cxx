/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_list.hxx"
#include "address_envelope.hxx"
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
        Add(pool, { &i.address, i.length });
}

bool
AddressList::Add(struct pool *pool, const SocketAddress address)
{
    if (addresses.full())
        return false;

    struct address_envelope *envelope = (struct address_envelope *)
        p_malloc(pool, sizeof(*envelope) - sizeof(envelope->address)
                 + address.GetSize());
    envelope->length = address.GetSize();
    memcpy(&envelope->address, address.GetAddress(), address.GetSize());

    addresses.append(envelope);
    return true;
}

const struct address_envelope *
AddressList::GetFirst() const
{
    if (addresses.empty())
        return nullptr;

    return addresses[0];
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
                                           &i.address, i.length);
        if (success)
            length += strlen(buffer + length);
    }

    buffer[length] = 0;

    return buffer;
}
