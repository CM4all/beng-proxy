/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address-list.h"
#include "address-envelope.h"
#include "pool.h"

#include <socket/address.h>

#include <assert.h>
#include <string.h>

void
address_list_copy(pool_t pool, struct address_list *dest,
                  const struct address_list *src)
{
    address_list_init(dest);
    dest->sticky = src->sticky;

    for (unsigned i = 0; i < src->size; ++i)
        address_list_add(pool, dest,
                         &src->addresses[i]->address,
                         src->addresses[i]->length);
}

bool
address_list_add(pool_t pool, struct address_list *al,
                 const struct sockaddr *address, socklen_t length)
{
    assert(al->size <= MAX_ADDRESSES);

    if (al->size >= MAX_ADDRESSES)
        return false;

    struct address_envelope *envelope = p_malloc(pool, sizeof(*envelope) -
                                                 sizeof(envelope->address) +
                                                 length);
    envelope->length = length;
    memcpy(&envelope->address, address, length);

    al->addresses[al->size++] = envelope;
    return true;
}

const struct address_envelope *
address_list_first(const struct address_list *al)
{
    assert(al->size <= MAX_ADDRESSES);

    if (al->size < 1)
        return NULL;

    return al->addresses[0];
}

const char *
address_list_key(const struct address_list *al)
{
    assert(al->size <= MAX_ADDRESSES);

    static char buffer[2048];
    size_t length = 0;
    bool success;

    for (unsigned i = 0; i < al->size; ++i) {
        const struct address_envelope *envelope = al->addresses[i];
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
