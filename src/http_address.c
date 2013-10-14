/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_address.h"
#include "uri-edit.h"
#include "uri-base.h"
#include "uri-relative.h"
#include "uri-verify.h"
#include "uri-extract.h"
#include "pool.h"
#include "strref.h"
#include "regex.h"

#include <socket/address.h>

#include <string.h>

static bool
uri_scheme_has_host(enum uri_scheme scheme)
{
    return scheme != URI_SCHEME_UNIX;
}

static struct http_address *
http_address_new(struct pool *pool, enum uri_scheme scheme,
                 const char *host_and_port, const char *path)
{
    assert(pool != NULL);
    assert(uri_scheme_has_host(scheme) == (host_and_port != NULL));
    assert(path != NULL);

    struct http_address *uwa = p_malloc(pool, sizeof(*uwa));
    uwa->scheme = scheme;
    uwa->host_and_port = host_and_port;
    uwa->path = path;
    uwa->expand_path = NULL;
    address_list_init(&uwa->addresses);
    return uwa;
}

/**
 * Utility function used by http_address_parse().
 */
static struct http_address *
http_address_parse2(struct pool *pool, enum uri_scheme scheme,
                    const char *uri, GError **error_r)
{
    assert(pool != NULL);
    assert(uri != NULL);

    const char *path = strchr(uri, '/');
    const char *host_and_port;
    if (path != NULL) {
        if (path == uri || !uri_path_verify_quick(path)) {
            g_set_error(error_r, http_address_quark(), 0,
                        "malformed HTTP URI");
            return NULL;
        }

        host_and_port = p_strndup(pool, uri, path - uri);
        path = p_strdup(pool, path);
    } else {
        host_and_port = p_strdup(pool, uri);
        path = "/";
    }

    return http_address_new(pool, scheme, host_and_port, path);
}

struct http_address *
http_address_parse(struct pool *pool, const char *uri, GError **error_r)
{
    if (memcmp(uri, "http://", 7) == 0)
        return http_address_parse2(pool, URI_SCHEME_HTTP, uri + 7, error_r);
    else if (memcmp(uri, "ajp://", 6) == 0)
        return http_address_parse2(pool, URI_SCHEME_AJP, uri + 6, error_r);
    else if (memcmp(uri, "unix:/", 6) == 0)
        return http_address_new(pool, URI_SCHEME_UNIX, NULL, uri + 5);

    g_set_error(error_r, http_address_quark(), 0,
                "unrecognized URI");
    return NULL;
}

struct http_address *
http_address_with_path(struct pool *pool, const struct http_address *uwa,
                       const char *path)
{
    struct http_address *p =
        http_address_new(pool, uwa->scheme, uwa->host_and_port, path);
    p->expand_path = p_strdup_checked(pool, uwa->expand_path);
    address_list_copy(pool, &p->addresses, &uwa->addresses);
    return p;
}

struct http_address *
http_address_dup(struct pool *pool, const struct http_address *uwa)
{
    assert(pool != NULL);
    assert(uwa != NULL);

    struct http_address *p =
        http_address_new(pool, uwa->scheme,
                         p_strdup(pool, uwa->host_and_port),
                         p_strdup(pool, uwa->path));

    p->expand_path = p_strdup_checked(pool, uwa->expand_path);
    address_list_copy(pool, &p->addresses, &uwa->addresses);

    return p;
}

struct http_address *
http_address_dup_with_path(struct pool *pool,
                           const struct http_address *uwa,
                           const char *path)
{
    struct http_address *p =
        http_address_new(pool, uwa->scheme,
                         p_strdup(pool, uwa->host_and_port),
                         path);
    p->expand_path = p_strdup_checked(pool, uwa->expand_path);
    address_list_copy(pool, &p->addresses, &uwa->addresses);
    return p;
}

G_GNUC_CONST
static const char *
uri_scheme_prefix(enum uri_scheme p)
{
    switch (p) {
    case URI_SCHEME_UNIX:
        return "unix:";

    case URI_SCHEME_HTTP:
        return "http://";

    case URI_SCHEME_AJP:
        return "ajp://";
    }

    assert(false);
    return NULL;
}

char *
http_address_absolute_with_path(struct pool *pool,
                                const struct http_address *uwa,
                                const char *path)
{
    assert(pool != NULL);
    assert(uwa != NULL);
    assert(uwa->host_and_port != NULL);
    assert(path != NULL);
    assert(*path == '/');

    return p_strcat(pool, uri_scheme_prefix(uwa->scheme),
                    uwa->host_and_port == NULL ? "" : uwa->host_and_port,
                    path, NULL);
}

char *
http_address_absolute(struct pool *pool, const struct http_address *uwa)
{
    assert(pool != NULL);
    assert(uwa != NULL);

    return http_address_absolute_with_path(pool, uwa, uwa->path);
}

struct http_address *
http_address_insert_query_string(struct pool *pool,
                                 const struct http_address *uwa,
                                 const char *query_string)
{
    return http_address_with_path(pool, uwa,
                                  uri_insert_query_string(pool, uwa->path,
                                                          query_string));
}

struct http_address *
http_address_insert_args(struct pool *pool,
                         const struct http_address *uwa,
                         const char *args, size_t args_length,
                         const char *path, size_t path_length)
{
    return http_address_with_path(pool, uwa,
                                  uri_insert_args(pool, uwa->path,
                                                  args, args_length,
                                                  path, path_length));
}

struct http_address *
http_address_save_base(struct pool *pool, const struct http_address *src,
                       const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(suffix != NULL);

    size_t length = base_string(src->path, suffix);
    if (length == (size_t)-1)
        return NULL;

    return http_address_dup_with_path(pool, src,
                                      p_strndup(pool, src->path, length));
}

struct http_address *
http_address_load_base(struct pool *pool, const struct http_address *src,
                       const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(suffix != NULL);
    assert(src->path != NULL);
    assert(*src->path != 0);
    assert(src->expand_path != NULL ||
           src->path[strlen(src->path) - 1] == '/');

    return http_address_dup_with_path(pool, src,
                                      p_strcat(pool, src->path, suffix, NULL));
}

const struct http_address *
http_address_apply(struct pool *pool, const struct http_address *src,
                   const char *relative, size_t relative_length)
{
    if (relative_length == 0)
        return src;

    if (uri_has_protocol(relative, relative_length)) {
        struct http_address *other =
            http_address_parse(pool, p_strndup(pool, relative, relative_length),
                               NULL);
        if (other == NULL || other->scheme != src->scheme)
            return NULL;

        if (uri_scheme_has_host(other->scheme) &&
            strcmp(other->host_and_port, src->host_and_port) != 0)
            /* if it points to a different host, we cannot apply the
               address list, and so this function must fail */
            return NULL;

        address_list_copy(pool, &other->addresses, &src->addresses);
        return other;
    }

    const char *p = uri_absolute(pool, src->path,
                                 relative, relative_length);
    assert(p != NULL);

    return http_address_with_path(pool, src, p);
}

const struct strref *
http_address_relative(const struct http_address *base,
                      const struct http_address *uwa,
                      struct strref *buffer)
{
    if (base->scheme != uwa->scheme)
        return NULL;

    if (uri_scheme_has_host(base->scheme) &&
        strcmp(base->host_and_port, uwa->host_and_port) != 0)
        return NULL;

    struct strref base_uri;
    strref_set_c(&base_uri, base->path);
    strref_set_c(buffer, uwa->path);
    return uri_relative(&base_uri, buffer);
}

bool
http_address_expand(struct pool *pool, struct http_address *uwa,
                    const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(uwa != NULL);
    assert(match_info != NULL);

    if (uwa->expand_path != NULL) {
        uwa->path = expand_string(pool, uwa->expand_path,
                                  match_info, error_r);
        if (uwa->path == NULL)
            return false;
    }

    return true;
}
