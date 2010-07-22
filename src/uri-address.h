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

/**
 * Duplicates this #uri_with_address object and inserts the specified
 * query string into the URI.
 */
struct uri_with_address *
uri_address_insert_query_string(pool_t pool,
                                const struct uri_with_address *uwa,
                                const char *query_string);

void
uri_address_add(struct uri_with_address *uwa,
                const struct sockaddr *addr, socklen_t addrlen);

const struct sockaddr *
uri_address_first(const struct uri_with_address *uwa, socklen_t *addrlen_r);

const struct sockaddr *
uri_address_next(struct uri_with_address *uwa, socklen_t *addrlen_r);

/**
 * Is there no more than one address?
 */
bool
uri_address_is_single(const struct uri_with_address *uwa);

/**
 * Generates a unique string which identifies this object in a hash
 * table.  This string stored in a statically allocated buffer.
 */
const char *
uri_address_key(const struct uri_with_address *uwa);

#endif
