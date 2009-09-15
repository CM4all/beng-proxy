/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_ADDRESS_H
#define __BENG_URI_ADDRESS_H

#include "pool.h"

#include <inline/list.h>

#include <sys/socket.h>

struct uri_with_address {
    pool_t pool;

    const char *uri;

    struct list_head addresses;
};

struct uri_with_address *
uri_address_new(pool_t pool, const char *uri);

struct uri_with_address *
uri_address_dup(pool_t pool, const struct uri_with_address *uwa);

void
uri_address_add(struct uri_with_address *uwa,
                const struct sockaddr *addr, socklen_t addrlen);

const struct sockaddr *
uri_address_next(struct uri_with_address *uwa, socklen_t *addrlen_r);

/**
 * Is there no more than one address?
 */
bool
uri_address_is_single(const struct uri_with_address *uwa);

#endif
