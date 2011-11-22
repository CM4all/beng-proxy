/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_ADDRESS_H
#define __BENG_URI_ADDRESS_H

#include "address-list.h"

#include <inline/list.h>
#include <inline/compiler.h>

#include <sys/socket.h>

#include <stddef.h>

struct pool;

struct uri_with_address {
    struct pool *pool;

    const char *uri;

    struct address_list addresses;
};

gcc_malloc
struct uri_with_address *
uri_address_new(struct pool *pool, const char *uri);

gcc_malloc
struct uri_with_address *
uri_address_dup(struct pool *pool, const struct uri_with_address *uwa);

/**
 * Duplicates this #uri_with_address object and inserts the specified
 * query string into the URI.
 */
gcc_malloc
struct uri_with_address *
uri_address_insert_query_string(struct pool *pool,
                                const struct uri_with_address *uwa,
                                const char *query_string);

/**
 * Duplicates this #uri_with_address object and inserts the specified
 * arguments into the URI.
 */
gcc_malloc
struct uri_with_address *
uri_address_insert_args(struct pool *pool,
                        const struct uri_with_address *uwa,
                        const char *args, size_t length);

void
uri_address_add(struct uri_with_address *uwa,
                const struct sockaddr *addr, socklen_t addrlen);

/**
 * Is there no more than one address?
 */
gcc_pure
bool
uri_address_is_single(const struct uri_with_address *uwa);

/**
 * Generates a unique string which identifies this object in a hash
 * table.  This string stored in a statically allocated buffer.
 */
gcc_pure
const char *
uri_address_key(const struct uri_with_address *uwa);

#endif
