/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-address.h"
#include "uri-relative.h"
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
        file_address_copy(pool, &dest->u.local, &src->u.local);
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
                             const char *args, size_t args_length,
                             const char *path, size_t path_length)
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
                                               args, args_length,
                                               path, path_length);
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
                                              args, args_length,
                                              path, path_length);

        if (src->u.cgi.path_info != NULL)
            dest->u.cgi.path_info =
                p_strncat(pool,
                          src->u.cgi.path_info, strlen(src->u.cgi.path_info),
                          ";", (size_t)1, args, args_length, path, path_length,
                          NULL);

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
        if (!file_address_save_base(pool, &dest->u.local, &src->u.local,
                                    suffix))
            return NULL;

        dest->type = src->type;
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
        file_address_load_base(pool, &dest->u.local, &src->u.local, suffix);
        dest->type = src->type;
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
        buffer->type = src->type;
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
        return cgi_address_id(pool, &address->u.cgi);
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

bool
resource_address_is_expandable(const struct resource_address *address)
{
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        return false;

    case RESOURCE_ADDRESS_LOCAL:
        return file_address_is_expandable(&address->u.local);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return cgi_address_is_expandable(&address->u.cgi);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return uri_address_is_expandable(address->u.http);
    }

    /* unreachable */
    assert(false);
    return false;
}

bool
resource_address_expand(struct pool *pool, struct resource_address *address,
                        const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(address != NULL);
    assert(match_info != NULL);

    switch (address->type) {
        struct uri_with_address *uwa;

    case RESOURCE_ADDRESS_NONE:
        return true;

    case RESOURCE_ADDRESS_LOCAL:
        return file_address_expand(pool, &address->u.local,
                                   match_info, error_r);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return cgi_address_expand(pool, &address->u.cgi, match_info, error_r);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        /* copy the uri_with_address object (it's a pointer, not
           in-line) and expand it */
        address->u.http = uwa = uri_address_dup(pool, address->u.http);
        return uri_address_expand(pool, uwa,
                                  match_info, error_r);
    }

    /* unreachable */
    assert(false);
    return true;
}
