/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_ADDRESS_H
#define __BENG_URI_ADDRESS_H

#include "address-list.h"

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

struct uri_with_address {
    enum uri_scheme scheme;

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
uri_address_quark(void)
{
    return g_quark_from_static_string("uri_address");
}

/**
 * Parse the given absolute URI into a newly allocated
 * #uri_with_address object.
 *
 * @return NULL on error
 */
gcc_malloc
struct uri_with_address *
uri_address_parse(struct pool *pool, const char *uri, GError **error_r);

/**
 * Create a new #uri_with_address object from the specified one, but
 * replace the "path" attribute.  The string pointers are stored,
 * they are not duplicated.
 */
gcc_malloc
struct uri_with_address *
uri_address_with_path(struct pool *pool,
                      const struct uri_with_address *uwa,
                      const char *path);

gcc_malloc
struct uri_with_address *
uri_address_dup(struct pool *pool, const struct uri_with_address *uwa);

/**
 * Create a new #uri_with_address object from the specified one, but
 * replace the "path" attribute.  The strings from the source object
 * are duplicated, but the "path" parameter is not.
 */
gcc_malloc
struct uri_with_address *
uri_address_dup_with_path(struct pool *pool,
                          const struct uri_with_address *uwa,
                          const char *path);

/**
 * Build the absolute URI from this object, but use the specified path
 * instead.
 */
gcc_malloc
char *
uri_address_absolute_with_path(struct pool *pool,
                               const struct uri_with_address *uwa,
                               const char *path);

/**
 * Build the absolute URI from this object.
 */
gcc_malloc
char *
uri_address_absolute(struct pool *pool, const struct uri_with_address *uwa);

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
                        const char *args, size_t args_length,
                        const char *path, size_t path_length);

gcc_malloc
struct uri_with_address *
uri_address_save_base(struct pool *pool, const struct uri_with_address *uwa,
                      const char *suffix);

gcc_malloc
struct uri_with_address *
uri_address_load_base(struct pool *pool, const struct uri_with_address *uwa,
                      const char *suffix);

const struct uri_with_address *
uri_address_apply(struct pool *pool, const struct uri_with_address *src,
                  const char *relative, size_t relative_length);

/**
 * Check if one #uri_with_address is relative to the base
 * #uri_with_address, and return the relative part.  Returns NULL if
 * both URIs do not match.
 */
const struct strref *
uri_address_relative(const struct uri_with_address *base,
                     const struct uri_with_address *uwa,
                     struct strref *buffer);

/**
 * Does this address need to be expanded with uri_address_expand()?
 */
gcc_pure
static inline bool
uri_address_is_expandable(const struct uri_with_address *address)
{
    assert(address != NULL);

    return address->expand_path != NULL;
}

bool
uri_address_expand(struct pool *pool, struct uri_with_address *uwa,
                   const GMatchInfo *match_info, GError **error_r);

#endif
