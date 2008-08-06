/*
 * Address of a resource, which might be a local file, a CGI script or
 * a HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-address.h"
#include "uri-relative.h"
#include "strref.h"

const struct resource_address *
resource_address_apply(pool_t pool, const struct resource_address *src,
                       const char *relative, size_t relative_length,
                       struct resource_address *buffer)
{
    const char *p;

    assert(pool != NULL);
    assert(src != NULL);
    assert(relative != NULL);

    switch (src->type) {
    case RESOURCE_ADDRESS_NONE:
        return NULL;

    case RESOURCE_ADDRESS_LOCAL:
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
        return NULL;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        strref_set_c(&base_uri, base->u.http->uri);
        strref_set_c(buffer, address->u.http->uri);
        return uri_relative(&base_uri, buffer);

    case RESOURCE_ADDRESS_CGI:
        /* XXX */
        strref_set_c(buffer, address->u.cgi.path_info);
        return buffer;
    }

    assert(false);
    return NULL;
}
