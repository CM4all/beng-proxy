/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_ADDRESS_H
#define __BENG_URI_ADDRESS_H

#include "address-list.h"

#include <inline/compiler.h>

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

#endif
