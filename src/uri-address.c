/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri-address.h"

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

