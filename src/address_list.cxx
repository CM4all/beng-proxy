/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address_list.hxx"
#include "address_envelope.hxx"
#include "pool.h"

#include <socket/address.h>

#include <assert.h>
#include <string.h>

void
address_list::CopyFrom(struct pool *pool, const struct address_list &src)
{
    Init();
    sticky_mode = src.sticky_mode;

    for (unsigned i = 0; i < src.size; ++i)
        Add(pool, &src.addresses[i]->address, src.addresses[i]->length);
}

bool
address_list::Add(struct pool *pool,
                  const struct sockaddr *address, size_t length)
{
    assert(size <= MAX_ADDRESSES);

    if (size >= MAX_ADDRESSES)
        return false;

    struct address_envelope *envelope = (struct address_envelope *)
        p_malloc(pool, sizeof(*envelope) - sizeof(envelope->address) + length);
    envelope->length = length;
    memcpy(&envelope->address, address, length);

    addresses[size++] = envelope;
    return true;
}

const struct address_envelope *
address_list::GetFirst() const
{
    assert(size <= address_list::MAX_ADDRESSES);

    if (size < 1)
        return nullptr;

    return addresses[0];
}

const char *
address_list::GetKey() const
{
    assert(size <= address_list::MAX_ADDRESSES);

    static char buffer[2048];
    size_t length = 0;
    bool success;

    for (unsigned i = 0; i < size; ++i) {
        const struct address_envelope *envelope = addresses[i];
        if (length > 0 && length < sizeof(buffer) - 1)
            buffer[length++] = ' ';

        success = socket_address_to_string(buffer + length,
                                           sizeof(buffer) - length,
                                           &envelope->address,
                                           envelope->length);
        if (success)
            length += strlen(buffer + length);
    }

    buffer[length] = 0;

    return buffer;
}
