/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "address-list.h"
#include "address-envelope.h"
#include "pool.h"

#include <socket/address.h>

#include <string.h>

struct address_item {
    struct list_head siblings;

    struct address_envelope envelope;
};

void
address_list_copy(pool_t pool, struct address_list *dest,
                  const struct address_list *src)
{
    address_list_init(dest);

    for (const struct address_item *item = (const struct address_item *)src->addresses.next;
         &item->siblings != &src->addresses;
         item = (const struct address_item *)item->siblings.next)
        address_list_add(pool, dest,
                         &item->envelope.address, item->envelope.length);
}

void
address_list_add(pool_t pool, struct address_list *al,
                 const struct sockaddr *address, socklen_t length)
{
    struct address_item *item = p_malloc(pool, sizeof(*item) -
                                         sizeof(item->envelope.address) +
                                         length);
    item->envelope.length = length;
    memcpy(&item->envelope.address, address, length);

    list_add(&item->siblings, &al->addresses);
}

const struct sockaddr *
address_list_first(const struct address_list *al, socklen_t *length_r)
{
    if (list_empty(&al->addresses))
        return NULL;

    struct address_item *item = (struct address_item *)al->addresses.next;
    *length_r = item->envelope.length;
    return &item->envelope.address;
}

const struct sockaddr *
address_list_next(struct address_list *al, socklen_t *length_r)
{
    struct address_item *ua;

    if (list_empty(&al->addresses))
        return NULL;

    ua = (struct address_item *)al->addresses.next;

    /* move to back */
    list_remove(&ua->siblings);
    list_add(&ua->siblings, al->addresses.prev);

    *length_r = ua->envelope.length;
    return &ua->envelope.address;
}

bool
address_list_is_single(const struct address_list *al)
{
    return al->addresses.next->next == &al->addresses;
}

const char *
address_list_key(const struct address_list *al)
{
    static char buffer[2048];
    size_t length = 0;
    const struct address_item *ua;
    bool success;

    for (ua = (const struct address_item *)al->addresses.next;
         ua != (const struct address_item *)&al->addresses;
         ua = (const struct address_item *)ua->siblings.next) {
        if (length > 0 && length < sizeof(buffer) - 1)
            buffer[length++] = ' ';

        success = socket_address_to_string(buffer + length,
                                           sizeof(buffer) - length,
                                           &ua->envelope.address,
                                           ua->envelope.length);
        if (success)
            length += strlen(buffer + length);
    }

    buffer[length] = 0;

    return buffer;
}
