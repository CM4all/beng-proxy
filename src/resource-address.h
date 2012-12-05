/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_RESOURCE_ADDRESS_H
#define __BENG_RESOURCE_ADDRESS_H

#include "pool.h"
#include "file-address.h"
#include "uri-address.h"
#include "cgi-address.h"
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
        struct file_address local;

        const struct uri_with_address *http;

        const struct cgi_address *cgi;
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

gcc_const
static inline struct cgi_address *
resource_address_get_cgi(struct resource_address *address)
{
    assert(resource_address_is_cgi_alike(address));

    union {
        const struct cgi_address *in;
        struct cgi_address *out;
    } u = { .in = address->u.cgi };
    return u.out;
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
 * Duplicate the #resource_address object, but replace the HTTP/AJP
 * URI path component.
 */
gcc_malloc
struct resource_address *
resource_address_dup_with_path(struct pool *pool,
                               const struct resource_address *src,
                               const char *path);

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
 * arguments and the path suffix.  If this resource address does not
 * support the operation, the original #resource_address pointer may
 * be returned.
 */
gcc_pure gcc_malloc
const struct resource_address *
resource_address_insert_args(struct pool *pool,
                             const struct resource_address *src,
                             const char *args, size_t args_length,
                             const char *path, size_t path_length);

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
 * Warning: this function does not check for excessive "../"
 * sub-strings.
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
resource_address_host_and_port(const struct resource_address *address);

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
bool
resource_address_is_expandable(const struct resource_address *address);

/**
 * Expand the expand_path_info attribute.
 */
bool
resource_address_expand(struct pool *pool, struct resource_address *address,
                        const GMatchInfo *match_info, GError **error_r);

#endif
