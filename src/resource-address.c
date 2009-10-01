/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-address.h"
#include "uri-relative.h"
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
        dest->u.local.content_type = src->u.local.content_type == NULL
            ? NULL : p_strdup(pool, src->u.local.content_type);
        dest->u.local.delegate = src->u.local.delegate == NULL
            ? NULL : p_strdup(pool, src->u.local.delegate);
        dest->u.local.document_root = src->u.local.document_root == NULL
            ? NULL : p_strdup(pool, src->u.local.document_root);
        break;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http != NULL);
        dest->u.http = uri_address_dup(pool, src->u.http);
        break;

    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
        assert(src->u.cgi.path != NULL);

        dest->u.cgi.path = p_strdup(pool, src->u.cgi.path);

        for (unsigned i = 0; i < src->u.cgi.num_args; ++i)
            dest->u.cgi.args[i] = p_strdup(pool, src->u.cgi.args[i]);
        dest->u.cgi.num_args = src->u.cgi.num_args;

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

static size_t
base_string(const char *p, const char *suffix)
{
    size_t length = strlen(p), suffix_length = strlen(suffix);

    return length > suffix_length && p[length - suffix_length - 1] == '/' &&
        memcmp(p + length - suffix_length, suffix, suffix_length) == 0
        ? length - suffix_length
        : 0;
}

struct resource_address *
resource_address_save_base(pool_t pool, const struct resource_address *src,
                           const char *suffix)
{
    struct resource_address *dest;
    size_t length;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
        return NULL;

    case RESOURCE_ADDRESS_LOCAL:
        length = base_string(src->u.local.path, suffix);
        if (length == 0)
            return NULL;

        dest = resource_address_dup(pool, src);
        dest->u.local.path = p_strndup(pool, dest->u.local.path, length);
        return dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        length = base_string(src->u.http->uri, suffix);
        if (length == 0)
            return NULL;

        dest = resource_address_dup(pool, src);
        dest->u.http->uri = p_strndup(pool, dest->u.http->uri, length);
        return dest;
    }

    assert(false);
    return NULL;
}

struct resource_address *
resource_address_load_base(pool_t pool, const struct resource_address *src,
                           const char *suffix)
{
    struct resource_address *dest;

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
        assert(false);
        return NULL;

    case RESOURCE_ADDRESS_LOCAL:
        assert(src->u.local.path != NULL);
        assert(*src->u.local.path != 0);
        assert(src->u.local.path[strlen(src->u.local.path) - 1] == '/');

        dest = resource_address_dup(pool, src);
        dest->u.local.path = p_strcat(pool, dest->u.local.path, suffix, NULL);
        return dest;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(src->u.http->uri != NULL);
        assert(*src->u.http->uri != 0);
        assert(src->u.http->uri[strlen(src->u.http->uri) - 1] == '/');

        dest = resource_address_dup(pool, src);
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
        /* XXX */
        strref_set_c(buffer, address->u.cgi.path_info);
        return buffer;
    }

    assert(false);
    return NULL;
}

static const char *
append_args(pool_t pool, const struct resource_address *address,
            const char *p)
{
    for (unsigned i = 0; i < address->u.cgi.num_args; ++i)
        p = p_strcat(pool, p, "!", address->u.cgi.args[i], NULL);

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
        return append_args(pool, address, p_strdup(pool, address->u.cgi.path));
    }

    assert(false);
    return "";
}
