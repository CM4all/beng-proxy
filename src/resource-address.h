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
#include "jail.h"

#include <glib.h>
#include <assert.h>

struct strref;

enum resource_address_type {
    RESOURCE_ADDRESS_NONE = 0,
    RESOURCE_ADDRESS_LOCAL,
    RESOURCE_ADDRESS_HTTP,
    RESOURCE_ADDRESS_PIPE,
    RESOURCE_ADDRESS_CGI,
    RESOURCE_ADDRESS_FASTCGI,
    RESOURCE_ADDRESS_WAS,
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
            struct jail_params jail;
        } local;

        struct uri_with_address *http;

        struct {
            const char *path;

            const char *args[32];
            unsigned num_args;

            struct jail_params jail;

            const char *interpreter;
            const char *action;

            const char *uri;
            const char *script_name, *path_info, *query_string;
            const char *document_root;

            /**
             * The value of #TRANSLATE_EXPAND_PATH_INFO.  Only used by
             * the translation cache.
             */
            const char *expand_path_info;

            /**
             * An optional list of addresses to connect to.  If given
             * for a FastCGI resource, then beng-proxy connects to one
             * of the addresses instead of spawning a new child
             * process.
             */
            struct address_list address_list;
        } cgi;
    } u;
};

/**
 * Is this a CGI address, or a similar protocol?
 */
static inline bool
resource_address_is_cgi_alike(const struct resource_address *address)
{
    return address->type == RESOURCE_ADDRESS_CGI ||
        address->type == RESOURCE_ADDRESS_FASTCGI ||
        address->type == RESOURCE_ADDRESS_WAS;
}

gcc_pure
static inline const char *
resource_address_cgi_uri(struct pool *pool,
                         const struct resource_address *address)
{
    const char *p;

    assert(resource_address_is_cgi_alike(address));

    if (address->u.cgi.uri != NULL)
        return address->u.cgi.uri;

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
resource_address_copy(struct pool *pool, struct resource_address *dest,
                      const struct resource_address *src);

gcc_malloc
static inline struct resource_address *
resource_address_dup(struct pool *pool, const struct resource_address *src)
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
gcc_pure gcc_malloc
const struct resource_address *
resource_address_insert_query_string_from(struct pool *pool,
                                          const struct resource_address *src,
                                          const char *uri);

/**
 * Duplicate this #resource_address object, and inserts the URI
 * arguments.  If this resource address does not support URI
 * arguments, the original #resource_address pointer is returned.
 */
gcc_pure gcc_malloc
const struct resource_address *
resource_address_insert_args(struct pool *pool,
                             const struct resource_address *src,
                             const char *args, size_t length);

/**
 * Check if a "base" URI can be generated automatically from this
 * #resource_address.  This applies when the CGI's PATH_INFO matches
 * the end of the specified URI.
 *
 * @param uri the request URI
 * @return a newly allocated base, or NULL if that is not possible
 */
gcc_malloc
char *
resource_address_auto_base(struct pool *pool,
                           const struct resource_address *address,
                           const char *uri);

/**
 * Duplicate a resource address, but return the base address.
 *
 * @param src the original resource address
 * @param suffix the suffix to be removed from #src
 * @return NULL if the suffix does not match, or if this address type
 * cannot have a base address
 */
gcc_malloc
struct resource_address *
resource_address_save_base(struct pool *pool, struct resource_address *dest,
                           const struct resource_address *src,
                           const char *suffix);

/**
 * Duplicate a resource address, and append a suffix.
 *
 * @param src the base resource address (must end with a slash)
 * @param suffix the suffix to be addded to #src
 * @return NULL if this address type cannot have a base address
 */
gcc_malloc
struct resource_address *
resource_address_load_base(struct pool *pool, struct resource_address *dest,
                           const struct resource_address *src,
                           const char *suffix);

gcc_pure
const struct resource_address *
resource_address_apply(struct pool *pool, const struct resource_address *src,
                       const char *relative, size_t relative_length,
                       struct resource_address *buffer);

gcc_pure
const struct strref *
resource_address_relative(const struct resource_address *base,
                          const struct resource_address *address,
                          struct strref *buffer);

/**
 * Generates a string identifying the address.  This can be used as a
 * key in a hash table.
 */
gcc_pure
const char *
resource_address_id(const struct resource_address *address, struct pool *pool);

/**
 * Determine the URI path.  May return NULL if unknown or not
 * applicable.
 */
gcc_pure
const char *
resource_address_host_and_port(const struct resource_address *address,
                               struct pool *pool);

/**
 * Determine the URI path.  May return NULL if unknown or not
 * applicable.
 */
gcc_pure
const char *
resource_address_uri_path(const struct resource_address *address);

/**
 * Does this address need to be expanded with
 * resource_address_expand()?
 */
gcc_pure
static inline bool
resource_address_is_expandable(const struct resource_address *address)
{
    assert(address != NULL);

    return resource_address_is_cgi_alike(address) &&
        address->u.cgi.expand_path_info;
}

/**
 * Expand the expand_path_info attribute.
 */
void
resource_address_expand(struct pool *pool, struct resource_address *address,
                        const GMatchInfo *match_info);

#endif
