/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-address.h"
#include "uri-relative.h"
#include "uri-verify.h"
#include "uri-escape.h"
#include "uri-edit.h"
#include "strref.h"

void
resource_address_copy(pool_t pool, struct resource_address *dest,
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
       assert(src->u.cgi.path != NULL);

        dest->u.cgi.path = p_strdup(pool, src->u.cgi.path);

        for (unsigned i = 0; i < src->u.cgi.num_args; ++i)
            dest->u.cgi.args[i] = p_strdup(pool, src->u.cgi.args[i]);
        dest->u.cgi.num_args = src->u.cgi.num_args;

        jail_params_copy(pool, &dest->u.cgi.jail, &src->u.cgi.jail);

        dest->u.cgi.interpreter =
            p_strdup_checked(pool, src->u.cgi.interpreter);
        dest->u.cgi.action = p_strdup_checked(pool, src->u.cgi.action);
        dest->u.cgi.uri =
            p_strdup_checked(pool, src->u.cgi.uri);
        dest->u.cgi.script_name =
            p_strdup_checked(pool, src->u.cgi.script_name);
        dest->u.cgi.path_info = p_strdup_checked(pool, src->u.cgi.path_info);
        dest->u.cgi.query_string =
            p_strdup_checked(pool, src->u.cgi.query_string);
        dest->u.cgi.document_root =
            p_strdup_checked(pool, src->u.cgi.document_root);

        if (src->type == RESOURCE_ADDRESS_FASTCGI)
            address_list_copy(pool, &dest->u.cgi.address_list,
                              &src->u.cgi.address_list);

        break;
    }
}

const struct resource_address *
resource_address_insert_query_string_from(pool_t pool,
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
resource_address_insert_args(pool_t pool,
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

/**
 * @return (size_t)-1 on mismatch
 */
static size_t
base_string(const char *p, const char *suffix)
{
    size_t length = strlen(p), suffix_length = strlen(suffix);

    if (length == suffix_length)
        /* special case: zero-length prefix (not followed by a
           slash) */
        return memcmp(p, suffix, length) == 0
            ? 0 : (size_t)-1;

    return length > suffix_length && p[length - suffix_length - 1] == '/' &&
        memcmp(p + length - suffix_length, suffix, suffix_length) == 0
        ? length - suffix_length
        : (size_t)-1;
}

/**
 * @return (size_t)-1 on mismatch
 */
static size_t
base_string_unescape(pool_t pool, const char *p, const char *suffix)
{
    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    return base_string(p, unescaped);
}

struct resource_address *
resource_address_save_base(pool_t pool, struct resource_address *dest,
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
        if (src->u.cgi.path_info == NULL)
            return NULL;

        length = base_string_unescape(pool, src->u.cgi.path_info, suffix);
        if (length == (size_t)-1)
            return NULL;

        resource_address_copy(pool, dest, src);
        dest->u.cgi.path_info = p_strndup(pool, dest->u.cgi.path_info, length);
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
        length = base_string(src->u.http->uri, suffix);
        if (length == (size_t)-1)
            return NULL;

        resource_address_copy(pool, dest, src);
        dest->u.http->uri = p_strndup(pool, dest->u.http->uri, length);
        return dest;
    }

    assert(false);
    return NULL;
}

struct resource_address *
resource_address_load_base(pool_t pool, struct resource_address *dest,
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
        if (src->u.cgi.path_info == NULL)
            return NULL;

        unescaped = p_strdup(pool, suffix);
        unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

        resource_address_copy(pool, dest, src);
        dest->u.cgi.path_info = p_strcat(pool, dest->u.cgi.path_info,
                                         unescaped, NULL);
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
        assert(src->u.http->uri != NULL);
        assert(*src->u.http->uri != 0);
        assert(src->u.http->uri[strlen(src->u.http->uri) - 1] == '/');

        resource_address_copy(pool, dest, src);
        dest->u.http->uri = p_strcat(pool, dest->u.http->uri, suffix, NULL);
        return dest;
    }

    assert(false);
    return NULL;
}

const struct resource_address *
resource_address_apply(pool_t pool, const struct resource_address *src,
                       const char *relative, size_t relative_length,
                       struct resource_address *buffer)
{
    const char *p;

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
        if (relative_length == 0)
            return src;

        p = uri_absolute(pool, src->u.http->uri, relative, relative_length);
        if (p == NULL)
            return NULL;

        resource_address_copy(pool, buffer, src);
        buffer->u.http->uri = p;
        return buffer;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        if (relative_length == 0)
            return src;

        /* XXX */
        p = uri_absolute(pool,
                         src->u.cgi.path_info == NULL ? "" : src->u.cgi.path_info,
                         relative, relative_length);
        if (p == NULL)
            return NULL;

        resource_address_copy(pool, buffer, src);
        buffer->u.cgi.path_info = p;
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
        strref_set_c(&base_uri, base->u.http->uri);
        strref_set_c(buffer, address->u.http->uri);
        return uri_relative(&base_uri, buffer);

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
append_args(pool_t pool, const struct resource_address *address,
            const char *p)
{
    if (address->u.cgi.jail.enabled)
        p = p_strcat(pool, p, ";j", NULL);

    if (address->u.cgi.interpreter != NULL)
        p = p_strcat(pool, p, ";i=", address->u.cgi.interpreter, NULL);

    if (address->u.cgi.action != NULL)
        p = p_strcat(pool, p, ";a=", address->u.cgi.action, NULL);

    for (unsigned i = 0; i < address->u.cgi.num_args; ++i)
        p = p_strcat(pool, p, "!", address->u.cgi.args[i], NULL);

    if (address->u.cgi.script_name != NULL)
        p = p_strcat(pool, p, ";s=", address->u.cgi.script_name, NULL);

    if (address->u.cgi.path_info != NULL)
        p = p_strcat(pool, p, ";p=", address->u.cgi.path_info, NULL);

    if (address->u.cgi.query_string != NULL)
        p = p_strcat(pool, p, "?", address->u.cgi.query_string, NULL);

    return p;
}

const char *
resource_address_id(const struct resource_address *address, pool_t pool)
{
    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        return "";

    case RESOURCE_ADDRESS_LOCAL:
        return p_strdup(pool, address->u.local.path);

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        return p_strdup(pool, address->u.http->uri);

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return append_args(pool, address, p_strdup(pool, address->u.cgi.path));
    }

    assert(false);
    return "";
}
