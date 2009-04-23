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
    RESOURCE_ADDRESS_CGI,
    RESOURCE_ADDRESS_FASTCGI,
    RESOURCE_ADDRESS_AJP
};

struct resource_address {
    enum resource_address_type type;

    union {
        struct {
            const char *path;
            const char *content_type;
        } local;

        struct uri_with_address *http;

        struct {
            const char *path;
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
