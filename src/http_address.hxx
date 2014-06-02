/*
 * The address of a resource stored on a HTTP (or AJP) server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_ADDRESS_HXX
#define BENG_PROXY_HTTP_ADDRESS_HXX

#include "address_list.hxx"

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
     * The host part of the URI (including the port, if any).  nullptr if
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

    /**
     * Build the absolute URI from this object, but use the specified path
     * instead.
     */
    gcc_malloc
    char *GetAbsoluteURI(struct pool *pool, const char *override_path) const;

    /**
     * Build the absolute URI from this object.
     */
    gcc_malloc
    char *GetAbsoluteURI(struct pool *pool) const;

    /**
     * Duplicates this #http_address object and inserts the specified
     * query string into the URI.
     */
    gcc_malloc
    struct http_address *InsertQueryString(struct pool *pool,
                                           const char *query_string) const;

    /**
     * Duplicates this #http_address object and inserts the specified
     * arguments into the URI.
     */
    gcc_malloc
    struct http_address *InsertArgs(struct pool *pool,
                                    const char *args, size_t args_length,
                                    const char *path_info,
                                    size_t path_info_length) const;

    gcc_pure
    bool IsValidBase() const;

    gcc_malloc
    struct http_address *SaveBase(struct pool *pool, const char *suffix) const;

    gcc_malloc
    struct http_address *LoadBase(struct pool *pool, const char *suffix) const;

    const struct http_address *Apply(struct pool *pool, const char *relative,
                                     size_t relative_length) const;

    /**
     * Does this address need to be expanded with http_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_path != nullptr;
    }

    bool Expand(struct pool *pool, const GMatchInfo *match_info,
                GError **error_r);

    gcc_pure
    int GetDefaultPort() const {
        switch (scheme) {
        case URI_SCHEME_UNIX:
            return 0;

        case URI_SCHEME_HTTP:
            return ssl ? 443 : 80;

        case URI_SCHEME_AJP:
            return 8009;
        }

        gcc_unreachable();
    }
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
 * @return nullptr on error
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
 * Check if one #http_address is relative to the base
 * #http_address, and return the relative part.  Returns nullptr if
 * both URIs do not match.
 */
const struct strref *
http_address_relative(const struct http_address *base,
                      const struct http_address *uwa,
                      struct strref *buffer);

#endif
