/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_RESOURCE_ADDRESS_H
#define __BENG_RESOURCE_ADDRESS_H

#include "pool.h"
#include "uri-address.h"

#include <assert.h>

struct strref;

enum resource_address_type {
    RESOURCE_ADDRESS_NONE = 0,
    RESOURCE_ADDRESS_LOCAL,
    RESOURCE_ADDRESS_HTTP,
    RESOURCE_ADDRESS_PIPE,
    RESOURCE_ADDRESS_CGI,
    RESOURCE_ADDRESS_FASTCGI,
    RESOURCE_ADDRESS_AJP
};

struct resource_address {
    enum resource_address_type type;

    union {
        struct {
            const char *path;
            const char *deflated;
            const char *gzipped;
            const char *content_type;
            const char *delegate;
            const char *document_root;

            /**
             * Should the delegate be jailed?
             */
            bool jail;
        } local;

        struct uri_with_address *http;

        struct {
            const char *path;

            const char *args[32];
            unsigned num_args;

            bool jail;
            const char *interpreter;
            const char *action;

            const char *script_name, *path_info, *query_string;
            const char *document_root;
        } cgi;
    } u;
};

static inline const char *
resource_address_cgi_uri(pool_t pool, const struct resource_address *address)
{
    const char *p;

    assert(address->type == RESOURCE_ADDRESS_CGI ||
           address->type == RESOURCE_ADDRESS_FASTCGI);

    p = address->u.cgi.script_name;
    if (p == NULL)
        p = "";

    if (address->u.cgi.path_info != NULL)
        p = p_strcat(pool, p, address->u.cgi.path_info, NULL);

    if (address->u.cgi.query_string != NULL)
        p = p_strcat(pool, p, "?", address->u.cgi.query_string, NULL);

    return p;
}

void
resource_address_copy(pool_t pool, struct resource_address *dest,
                      const struct resource_address *src);

static inline struct resource_address *
resource_address_dup(pool_t pool, const struct resource_address *src)
{
    struct resource_address *dest = p_malloc(pool, sizeof(*dest));

    resource_address_copy(pool, dest, src);
    return dest;
}

/**
 * Duplicate this #resource_address object, and inserts the query
 * string from the specified URI.  If this resource address does not
 * support a query string, or if the URI does not have one, the
 * original #resource_address pointer is returned.
 */
const struct resource_address *
resource_address_insert_query_string_from(pool_t pool,
                                          const struct resource_address *src,
                                          const char *uri);

/**
 * Duplicate a resource address, but return the base address.
 *
 * @param src the original resource address
 * @param suffix the suffix to be removed from #src
 * @return NULL if the suffix does not match, or if this address type
 * cannot have a base address
 */
struct resource_address *
resource_address_save_base(pool_t pool, const struct resource_address *src,
                           const char *suffix);

/**
 * Duplicate a resource address, and append a suffix.
 *
 * @param src the base resource address (must end with a slash)
 * @param suffix the suffix to be addded to #src
 * @return NULL if this address type cannot have a base address
 */
struct resource_address *
resource_address_load_base(pool_t pool, const struct resource_address *src,
                           const char *suffix);

const struct resource_address *
resource_address_apply(pool_t pool, const struct resource_address *src,
                       const char *relative, size_t relative_length,
                       struct resource_address *buffer);

const struct strref *
resource_address_relative(const struct resource_address *base,
                          const struct resource_address *address,
                          struct strref *buffer);

/**
 * Generates a string identifying the address.  This can be used as a
 * key in a hash table.
 */
const char *
resource_address_id(const struct resource_address *address, pool_t pool);

#endif
