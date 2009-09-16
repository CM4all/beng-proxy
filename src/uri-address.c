/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-address.h"

#include <socket/address.h>

#include <string.h>

struct uri_address {
    struct list_head siblings;

    socklen_t length;

    struct sockaddr address;
};

struct uri_with_address *
uri_address_new(pool_t pool, const char *uri)
{
    struct uri_with_address *uwa = p_malloc(pool, sizeof(*uwa));
    uwa->pool = pool;
    uwa->uri = p_strdup(pool, uri);
    list_init(&uwa->addresses);

    return uwa;
}

struct uri_with_address *
uri_address_dup(pool_t pool, const struct uri_with_address *uwa)
{
    struct uri_with_address *p = p_malloc(pool, sizeof(*uwa));
    const struct uri_address *ua;

    p->pool = pool;
    p->uri = p_strdup(pool, uwa->uri);
    list_init(&p->addresses);

    for (ua = (const struct uri_address *)uwa->addresses.next;
         ua != (const struct uri_address *)&uwa->addresses;
         ua = (const struct uri_address *)ua->siblings.next)
        uri_address_add(p, &ua->address, ua->length);

    return p;
}

void
uri_address_add(struct uri_with_address *uwa,
                const struct sockaddr *addr, socklen_t addrlen)
{
    struct uri_address *ua = p_malloc(uwa->pool, sizeof(*ua) -
                                      sizeof(ua->address) + addrlen);
    ua->length = addrlen;
    memcpy(&ua->address, addr, addrlen);

    list_add(&ua->siblings, &uwa->addresses);
}

const struct sockaddr *
uri_address_first(const struct uri_with_address *uwa, socklen_t *addrlen_r)
{
    struct uri_address *ua;

    if (list_empty(&uwa->addresses))
        return NULL;

    ua = (struct uri_address *)uwa->addresses.next;
    *addrlen_r = ua->length;
    return &ua->address;
}

const struct sockaddr *
uri_address_next(struct uri_with_address *uwa, socklen_t *addrlen_r)
{
    struct uri_address *ua;

    if (list_empty(&uwa->addresses))
        return NULL;

    ua = (struct uri_address *)uwa->addresses.next;

    /* move to back */
    list_remove(&ua->siblings);
    list_add(&ua->siblings, uwa->addresses.prev);

    *addrlen_r = ua->length;
    return &ua->address;
}

bool
uri_address_is_single(const struct uri_with_address *uwa)
{
    return uwa->addresses.next->next == &uwa->addresses;
}

const char *
uri_address_key(const struct uri_with_address *uwa)
{
    static char buffer[2048];
    size_t length = 0;
    const struct uri_address *ua;
    bool success;

    for (ua = (const struct uri_address *)uwa->addresses.next;
         ua != (const struct uri_address *)&uwa->addresses;
         ua = (const struct uri_address *)ua->siblings.next) {
        if (length > 0 && length < sizeof(buffer) - 1)
            buffer[length++] = ' ';

        success = socket_address_to_string(buffer + length,
                                           sizeof(buffer) - length,
                                           &ua->address, ua->length);
        if (success)
            length += strlen(buffer + length);
    }

    buffer[length] = 0;

    return buffer;
}
