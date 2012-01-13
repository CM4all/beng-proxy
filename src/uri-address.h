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

gcc_malloc
struct uri_with_address *
uri_address_save_base(struct pool *pool, const struct uri_with_address *uwa,
                      const char *suffix);

gcc_malloc
struct uri_with_address *
uri_address_load_base(struct pool *pool, const struct uri_with_address *uwa,
                      const char *suffix);

/**
 * Check if one #uri_with_address is relative to the base
 * #uri_with_address, and return the relative part.  Returns NULL if
 * both URIs do not match.
 */
const struct strref *
uri_address_relative(const struct uri_with_address *base,
                     const struct uri_with_address *uwa,
                     struct strref *buffer);

#endif
