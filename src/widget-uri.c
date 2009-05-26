/*
 * Determine the real URI of a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "uri-relative.h"
#include "uri-parser.h"
#include "args.h"
#include "tpool.h"
#include "strref-pool.h"
#include "uri-address.h"

#include <assert.h>

void
widget_determine_address(pool_t pool, struct widget *widget)
{
    const char *path_info, *uri;
    struct resource_address *address;

    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(widget->lazy.address == NULL);

    path_info = widget_get_path_info(widget);
    assert(path_info != NULL);

    switch (widget->class->address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        break;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(widget->class->address.u.http->uri != NULL);

        if (strref_is_empty(&widget->from_request.query_string) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        uri = widget->class->address.u.http->uri;

        if (!strref_is_empty(&widget->from_request.query_string))
            uri = p_strncat(pool,
                            uri, strlen(uri),
                            path_info, strlen(path_info),
                            "?", (size_t)1,
                            widget->from_request.query_string.data,
                            widget->from_request.query_string.length,
                            NULL);
        else if (*path_info != 0)
            uri = p_strcat(pool,
                           uri,
                           path_info,
                           NULL);

        if (widget->query_string != NULL)
            uri = p_strcat(pool,
                           uri,
                           strchr(uri, '?') == NULL ? "?" : "&",
                           widget->query_string,
                           NULL);

        address = resource_address_dup(pool, &widget->class->address);
        address->u.http->uri = uri;
        widget->lazy.address = address;
        return;

    case RESOURCE_ADDRESS_CGI:
        if (strref_is_empty(&widget->from_request.query_string) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        address = resource_address_dup(pool, &widget->class->address);

        if (*path_info != 0)
            address->u.cgi.path_info = path_info;

        if (strref_is_empty(&widget->from_request.query_string))
            address->u.cgi.query_string = widget->query_string;
        else if (widget->query_string == NULL)
            address->u.cgi.query_string =
                strref_dup(pool, &widget->from_request.query_string);
        else
            address->u.cgi.query_string =
                p_strncat(pool,
                          widget->from_request.query_string.data,
                          widget->from_request.query_string.length,
                          "&", (size_t)1,
                          widget->query_string, strlen(widget->query_string),
                          NULL);

        widget->lazy.address = address;
        return;

    case RESOURCE_ADDRESS_FASTCGI:
        if (strref_is_empty(&widget->from_request.query_string) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        address = resource_address_dup(pool, &widget->class->address);

        if (*path_info != 0)
            address->u.cgi.path_info = path_info;

        if (strref_is_empty(&widget->from_request.query_string))
            address->u.cgi.query_string = widget->query_string;
        else if (widget->query_string == NULL)
            address->u.cgi.query_string =
                strref_dup(pool, &widget->from_request.query_string);
        else
            address->u.cgi.query_string =
                p_strncat(pool,
                          widget->from_request.query_string.data,
                          widget->from_request.query_string.length,
                          "&", (size_t)1,
                          widget->query_string, strlen(widget->query_string),
                          NULL);

        widget->lazy.address = address;
        return;
    }

    widget->lazy.address = &widget->class->address;
    return;
}

const char *
widget_absolute_uri(pool_t pool, struct widget *widget,
                    const struct strref *relative_uri)
{
    const char *base;

    assert(widget_address(pool, widget)->type == RESOURCE_ADDRESS_HTTP);

    base = widget_address(pool, widget)->u.http->uri;
    if (relative_uri == NULL)
        return base;

    return uri_absolute(pool, base, relative_uri->data, relative_uri->length);
}

const char *
widget_translation_uri(pool_t pool,
                       const struct parsed_uri *external_uri,
                       struct strmap *args,
                       const char *translation)
{
    const char *args2;

    args2 = args_format(pool, args,
                        "translate", translation,
                        NULL, NULL,
                        "frame");

    return p_strncat(pool,
                     external_uri->base.data,
                     external_uri->base.length,
                     ";", (size_t)1,
                     args2, strlen(args2),
                     NULL);
}

static const struct strref *
widget_relative_uri(pool_t pool, struct widget *widget,
                    const char *relative_uri, size_t relative_uri_length,
                    struct strref *buffer)
{
    struct resource_address address_buffer;
    const struct resource_address *address;

    address = resource_address_apply(pool, widget_address(pool, widget),
                                     relative_uri, relative_uri_length,
                                     &address_buffer);
    if (address == NULL)
        return NULL;

    return resource_address_relative(&widget->class->address, address, buffer);
}

const char *
widget_external_uri(pool_t pool,
                    const struct parsed_uri *external_uri,
                    struct strmap *args,
                    struct widget *widget,
                    const struct strref *relative_uri,
                    const char *frame, bool raw)
{
    const char *path;
    const char *new_uri;
    const char *args2;
    struct strref buffer;
    const struct strref *p;
    struct pool_mark mark;

    assert(frame != NULL || !raw);

    path = widget_path(widget);
    if (path == NULL ||
        external_uri == NULL ||
        widget->class == &root_widget_class)
        return NULL;

    pool_mark(tpool, &mark);

    if (relative_uri != NULL) {
        p = widget_relative_uri(tpool, widget,
                                relative_uri->data, relative_uri->length,
                                &buffer);
        if (p == NULL) {
            pool_rewind(tpool, &mark);
            return NULL;
        }
    } else
        p = NULL;

    /* the URI is relative to the widget's base URI.  Convert the URI
       into an absolute URI to the template page on this server and
       add the appropriate args. */
    args2 = args_format_n(tpool, args,
                          "focus", path, strlen(path),
                          "path",
                          p == NULL ? NULL : p->data,
                          p == NULL ? (size_t)0 : p->length,
                          frame == NULL ? NULL : "frame", frame,
                          frame == NULL ? 0 : strlen(frame),
                          NULL);

    new_uri = p_strncat(pool,
                        external_uri->base.data,
                        external_uri->base.length,
                        ";", (size_t)1,
                        args2, strlen(args2),
                        "&raw=1", (size_t)(raw ? 6 : 0),
                        NULL);

    pool_rewind(tpool, &mark);
    return new_uri;
}
