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
};

struct resource_address {
    enum resource_address_type type;

    union {
        const char *path;

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

    assert(address->type == RESOURCE_ADDRESS_CGI);

    p = address->u.cgi.script_name;
    if (p == NULL)
        p = "";

    if (address->u.cgi.path_info != NULL)
        p = p_strcat(pool, p, address->u.cgi.path_info, NULL);

    if (address->u.cgi.query_string != NULL)
        p = p_strcat(pool, p, "?", address->u.cgi.query_string, NULL);

    return p;
}

static inline void
resource_address_copy(pool_t pool, struct resource_address *dest,
                      const struct resource_address *src)
{
    dest->type = src->type;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        assert(src->u.path != NULL);
        dest->u.path = p_strdup(pool, src->u.path);
        break;

    case RESOURCE_ADDRESS_HTTP:
        assert(src->u.http != NULL);
        dest->u.http = uri_address_dup(pool, src->u.http);
        break;

    case RESOURCE_ADDRESS_CGI:
        assert(src->u.cgi.path != NULL);

        dest->u.cgi.path = p_strdup(pool, src->u.cgi.path);
        dest->u.cgi.jail = src->u.cgi.jail;
        dest->u.cgi.interpreter = src->u.cgi.interpreter == NULL
            ? NULL : p_strdup(pool, src->u.cgi.interpreter);
        dest->u.cgi.action = src->u.cgi.action == NULL
            ? NULL : p_strdup(pool, src->u.cgi.action);
        dest->u.cgi.script_name = src->u.cgi.script_name == NULL
            ? NULL : p_strdup(pool, src->u.cgi.script_name);
        dest->u.cgi.path_info = src->u.cgi.path_info == NULL
            ? NULL : p_strdup(pool, src->u.cgi.path_info);
        dest->u.cgi.query_string = src->u.cgi.query_string == NULL
            ? NULL : p_strdup(pool, src->u.cgi.query_string);
        dest->u.cgi.document_root = src->u.cgi.document_root == NULL
            ? NULL : p_strdup(pool, src->u.cgi.document_root);
        break;
    }
}

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

#endif
