/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-address.h"
#include "uri-base.h"
#include "uri-relative.h"
#include "uri-verify.h"
#include "uri-escape.h"
#include "uri-edit.h"
#include "strref.h"

void
resource_address_copy(struct pool *pool, struct resource_address *dest,
                      const struct resource_address *src)
{
    dest->type = src->type;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        assert(src->u.local.path != NULL);
        dest->u.local.path = p_strdup(pool, src->u.local.path);
        dest->u.local.deflated = p_strdup_checked(pool, src->u.local.deflated);
        dest->u.local.gzipped = p_strdup_checked(pool, src->u.local.gzipped);
        dest->u.local.content_type =
            p_strdup_checked(pool, src->u.local.content_type);
        dest->u.local.delegate = p_strdup_checked(pool, src->u.local.delegate);
        dest->u.local.document_root =
            p_strdup_checked(pool, src->u.local.document_root);

        jail_params_copy(pool, &dest->u.local.jail, &src->u.local.jail);
        break;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http != NULL);
        dest->u.http = uri_address_dup(pool, src->u.http);
        break;

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        cgi_address_copy(pool, &dest->u.cgi, &src->u.cgi,
                         src->type == RESOURCE_ADDRESS_FASTCGI);
        break;
    }
}

struct resource_address *
resource_address_dup_with_path(struct pool *pool,
                               const struct resource_address *src,
                               const char *path)
{
    struct resource_address *dest = p_malloc(pool, sizeof(*dest));
    dest->type = src->type;
    dest->u.http = uri_address_dup_with_path(pool, src->u.http, path);
    return dest;
}

const struct resource_address *
resource_address_insert_query_string_from(struct pool *pool,
                                          const struct resource_address *src,
                                          const char *uri)
{
    const char *query_string;
    struct resource_address *dest;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        /* no query string support */
        return src;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http != NULL);

        query_string = strchr(uri, '?');
        if (query_string == NULL || *++query_string == 0)
            /* no query string in URI */
            return src;

        dest = p_malloc(pool, sizeof(*dest));
        dest->type = src->type;
        dest->u.http = uri_address_insert_query_string(pool, src->u.http,
                                                       query_string);
        return dest;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        assert(src->u.cgi.path != NULL);

        query_string = strchr(uri, '?');
        if (query_string == NULL || *++query_string == 0)
            /* no query string in URI */
            return src;

        dest = p_malloc(pool, sizeof(*dest));
        resource_address_copy(pool, dest, src);

        if (dest->u.cgi.query_string != NULL)
            dest->u.cgi.query_string = p_strcat(pool, query_string, "&",
                                                dest->u.cgi.query_string, NULL);
        else
            dest->u.cgi.query_string = p_strdup(pool, query_string);
        return dest;
    }

    /* unreachable */
    assert(false);
    return src;
}

const struct resource_address *
resource_address_insert_args(struct pool *pool,
                             const struct resource_address *src,
                             const char *args, size_t length)
{
    struct resource_address *dest;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        /* no arguments support */
        return src;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http != NULL);

        dest = p_malloc(pool, sizeof(*dest));
        dest->type = src->type;
        dest->u.http = uri_address_insert_args(pool, src->u.http,
                                               args, length);
        return dest;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        assert(src->u.cgi.path != NULL);

        if (src->u.cgi.uri == NULL && src->u.cgi.path_info == NULL)
            return src;

        dest = p_malloc(pool, sizeof(*dest));
        resource_address_copy(pool, dest, src);

        if (src->u.cgi.uri != NULL)
            dest->u.cgi.uri = uri_insert_args(pool, src->u.cgi.uri,
                                              args, length);

        if (src->u.cgi.path_info != NULL)
            dest->u.cgi.path_info =
                p_strncat(pool,
                          src->u.cgi.path_info, strlen(src->u.cgi.path_info),
                          ";", (size_t)1, args, length, NULL);

        return dest;
    }

    /* unreachable */
    assert(false);
    return src;
}

char *
resource_address_auto_base(struct pool *pool,
                           const struct resource_address *address,
                           const char *uri)
{
    assert(pool != NULL);
    assert(address != NULL);
    assert(uri != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return NULL;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return cgi_address_auto_base(pool, &address->u.cgi, uri);
    }

    assert(false);
    return NULL;

}

struct resource_address *
resource_address_save_base(struct pool *pool, struct resource_address *dest,
                           const struct resource_address *src,
                           const char *suffix)
{
    assert(src != dest);

    size_t length;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
        return NULL;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        if (!cgi_address_save_base(pool, &dest->u.cgi, &src->u.cgi, suffix,
                                   src->type == RESOURCE_ADDRESS_FASTCGI))
            return NULL;

        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_LOCAL:
        length = base_string_unescape(pool, src->u.local.path, suffix);
        if (length == (size_t)-1)
            return NULL;

        resource_address_copy(pool, dest, src);
        dest->u.local.path = p_strndup(pool, dest->u.local.path, length);

        /* BASE+DEFLATED is not supported */
        dest->u.local.deflated = NULL;
        dest->u.local.gzipped = NULL;
        return dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        dest->u.http = uri_address_save_base(pool, src->u.http, suffix);
        if (dest->u.http == NULL)
            return NULL;

        dest->type = src->type;
        return dest;
    }

    assert(false);
    return NULL;
}

struct resource_address *
resource_address_load_base(struct pool *pool, struct resource_address *dest,
                           const struct resource_address *src,
                           const char *suffix)
{
    assert(src != dest);

    char *unescaped;

    if (!uri_path_verify_paranoid(suffix))
        return NULL;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
        assert(false);
        return NULL;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        cgi_address_load_base(pool, &dest->u.cgi, &src->u.cgi, suffix,
                              src->type == RESOURCE_ADDRESS_FASTCGI);
        dest->type = src->type;
        return dest;

    case RESOURCE_ADDRESS_LOCAL:
        assert(src->u.local.path != NULL);
        assert(*src->u.local.path != 0);
        assert(src->u.local.path[strlen(src->u.local.path) - 1] == '/');

        unescaped = p_strdup(pool, suffix);
        unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

        resource_address_copy(pool, dest, src);
        dest->u.local.path = p_strcat(pool, dest->u.local.path,
                                      unescaped, NULL);
        return dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        dest->u.http = uri_address_load_base(pool, src->u.http, suffix);
        if (dest->u.http == NULL)
            return NULL;

        dest->type = src->type;
        return dest;
    }

    assert(false);
    return NULL;
}

const struct resource_address *
resource_address_apply(struct pool *pool, const struct resource_address *src,
                       const char *relative, size_t relative_length,
                       struct resource_address *buffer)
{
    const struct uri_with_address *uwa;
    const struct cgi_address *cgi;

    assert(pool != NULL);
    assert(src != NULL);
    assert(relative != NULL || relative_length == 0);

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
        return NULL;

    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        return src;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        uwa = uri_address_apply(pool, src->u.http, relative, relative_length);
        if (uwa == NULL)
            return NULL;

        if (uwa == src->u.http)
            return src;

        buffer->type = src->type;
        buffer->u.http = uwa;
        return buffer;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        cgi = cgi_address_apply(pool, &buffer->u.cgi, &src->u.cgi,
                                relative, relative_length,
                                src->type == RESOURCE_ADDRESS_FASTCGI);
        if (cgi == NULL)
            return NULL;

        if (cgi == &src->u.cgi)
            return src;

        assert(cgi == &buffer->u.cgi);
        return buffer;
    }

    assert(false);
    return NULL;
}

const struct strref *
resource_address_relative(const struct resource_address *base,
                          const struct resource_address *address,
                          struct strref *buffer)
{
    struct strref base_uri;

    assert(base != NULL);
    assert(address != NULL);
    assert(base->type == address->type);
    assert(buffer != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        return NULL;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return uri_address_relative(base->u.http, address->u.http, buffer);

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        strref_set_c(&base_uri, base->u.cgi.path_info != NULL
                     ? base->u.cgi.path_info : "");
        strref_set_c(buffer, address->u.cgi.path_info != NULL
                     ? address->u.cgi.path_info : "");
        return uri_relative(&base_uri, buffer);
    }

    assert(false);
    return NULL;
}

static const char *
append_args(struct pool *pool, const struct resource_address *address,
            const char *p)
{
    for (unsigned i = 0; i < address->u.cgi.num_args; ++i)
        p = p_strcat(pool, p, "!", address->u.cgi.args[i], NULL);

    return p;
}

const char *
resource_address_id(const struct resource_address *address, struct pool *pool)
{
    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        return "";

    case RESOURCE_ADDRESS_LOCAL:
        return p_strdup(pool, address->u.local.path);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return uri_address_absolute(pool, address->u.http);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return append_args(pool, address, p_strdup(pool, address->u.cgi.path));
    }

    assert(false);
    return "";
}

const char *
resource_address_host_and_port(const struct resource_address *address,
                               struct pool *pool)
{
    assert(address != NULL);
    assert(pool != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return NULL;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return address->u.http->host_and_port;
    }

    /* unreachable */
    assert(false);
    return NULL;
}

const char *
resource_address_uri_path(const struct resource_address *address)
{
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        return NULL;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return address->u.http->path;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        if (address->u.cgi.uri != NULL)
            return address->u.cgi.uri;

        return address->u.cgi.script_name;
    }

    /* unreachable */
    assert(false);
    return NULL;
}

static const char *
expand_string(struct pool *pool, const char *src, const GMatchInfo *match_info)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(match_info != NULL);

    char *p = g_match_info_expand_references(match_info, src, NULL);
    if (p == NULL)
        /* XXX an error has occurred; how to report to the caller? */
        return src;

    /* move result to the memory pool */
    char *q = p_strdup(pool, p);
    g_free(p);
    return q;
}

/**
 * Expand EXPAND_PATH_INFO specifications in a #resource_address.
 */
void
resource_address_expand(struct pool *pool, struct resource_address *address,
                        const GMatchInfo *match_info)
{
    assert(pool != NULL);
    assert(address != NULL);
    assert(match_info != NULL);

    if (resource_address_is_cgi_alike(address) &&
        address->u.cgi.expand_path_info != NULL)
        address->u.cgi.path_info =
            expand_string(pool, address->u.cgi.expand_path_info, match_info);
}
