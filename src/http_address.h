/*
 * The address of a resource stored on a HTTP (or AJP) server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_ADDRESS_H
#define BENG_PROXY_HTTP_ADDRESS_H

#include "address_list.h"

#include <inline/compiler.h>

#include <glib.h>
#include <stddef.h>

struct pool;

enum uri_scheme {
    /**
     * HTTP over UNIX domain socket.
     */
    URI_SCHEME_UNIX,

    /**
     * HTTP over TCP.
     */
    URI_SCHEME_HTTP,

    /**
     * AJP over TCP.
     */
    URI_SCHEME_AJP,
};

struct http_address {
    enum uri_scheme scheme;

    bool ssl;

    /**
     * The host part of the URI (including the port, if any).  NULL if
     * scheme is URI_SCHEME_UNIX.
     */
    const char *host_and_port;

    /**
     * The path component of the URI, starting with a slash.
     */
    const char *path;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;

    struct address_list addresses;
};

G_GNUC_CONST
static inline GQuark
http_address_quark(void)
{
    return g_quark_from_static_string("http_address");
}

/**
 * Parse the given absolute URI into a newly allocated
 * #http_address object.
 *
 * @return NULL on error
 */
gcc_malloc
struct http_address *
http_address_parse(struct pool *pool, const char *uri, GError **error_r);

/**
 * Create a new #http_address object from the specified one, but
 * replace the "path" attribute.  The string pointers are stored,
 * they are not duplicated.
 */
gcc_malloc
struct http_address *
http_address_with_path(struct pool *pool,
                       const struct http_address *uwa,
                       const char *path);

gcc_malloc
struct http_address *
http_address_dup(struct pool *pool, const struct http_address *uwa);

/**
 * Create a new #http_address object from the specified one, but
 * replace the "path" attribute.  The strings from the source object
 * are duplicated, but the "path" parameter is not.
 */
gcc_malloc
struct http_address *
http_address_dup_with_path(struct pool *pool,
                           const struct http_address *uwa,
                           const char *path);

/**
 * Build the absolute URI from this object, but use the specified path
 * instead.
 */
gcc_malloc
char *
http_address_absolute_with_path(struct pool *pool,
                                const struct http_address *uwa,
                                const char *path);

/**
 * Build the absolute URI from this object.
 */
gcc_malloc
char *
http_address_absolute(struct pool *pool, const struct http_address *uwa);

/**
 * Duplicates this #http_address object and inserts the specified
 * query string into the URI.
 */
gcc_malloc
struct http_address *
http_address_insert_query_string(struct pool *pool,
                                 const struct http_address *uwa,
                                 const char *query_string);

/**
 * Duplicates this #http_address object and inserts the specified
 * arguments into the URI.
 */
gcc_malloc
struct http_address *
http_address_insert_args(struct pool *pool,
                         const struct http_address *uwa,
                         const char *args, size_t args_length,
                         const char *path, size_t path_length);

gcc_malloc
struct http_address *
http_address_save_base(struct pool *pool, const struct http_address *uwa,
                       const char *suffix);

gcc_malloc
struct http_address *
http_address_load_base(struct pool *pool, const struct http_address *uwa,
                       const char *suffix);

const struct http_address *
http_address_apply(struct pool *pool, const struct http_address *src,
                   const char *relative, size_t relative_length);

/**
 * Check if one #http_address is relative to the base
 * #http_address, and return the relative part.  Returns NULL if
 * both URIs do not match.
 */
const struct strref *
http_address_relative(const struct http_address *base,
                      const struct http_address *uwa,
                      struct strref *buffer);

/**
 * Does this address need to be expanded with http_address_expand()?
 */
gcc_pure
static inline bool
http_address_is_expandable(const struct http_address *address)
{
    assert(address != NULL);

    return address->expand_path != NULL;
}

bool
http_address_expand(struct pool *pool, struct http_address *uwa,
                   const GMatchInfo *match_info, GError **error_r);

gcc_pure
static inline int
http_address_default_port(const struct http_address *address)
{
    switch (address->scheme) {
    case URI_SCHEME_UNIX:
        return 0;

    case URI_SCHEME_HTTP:
        return address->ssl ? 443 : 80;

    case URI_SCHEME_AJP:
        return 8009;
    }

    gcc_unreachable();
}

#endif
