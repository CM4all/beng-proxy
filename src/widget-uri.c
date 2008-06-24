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
#include "uri-address.h"

#include <assert.h>

void
widget_determine_address(pool_t pool, struct widget *widget)
{
    const char *uri;
    struct resource_address *address;

    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(widget->from_request.path_info != NULL);
    assert(widget->lazy.address == NULL);

    switch (widget->class->address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
        break;

    case RESOURCE_ADDRESS_HTTP:
        assert(widget->class->address.u.http->uri != NULL);

        if (strref_is_empty(&widget->from_request.query_string) &&
            *widget->from_request.path_info == 0 &&
            widget->query_string == NULL)
            break;

        uri = widget->class->address.u.http->uri;

        if (!strref_is_empty(&widget->from_request.query_string))
            uri = p_strncat(pool,
                            uri, strlen(uri),
                            widget->from_request.path_info,
                            strlen(widget->from_request.path_info),
                            "?", (size_t)1,
                            widget->from_request.query_string.data,
                            widget->from_request.query_string.length,
                            NULL);
        else if (*widget->from_request.path_info != 0)
            uri = p_strcat(pool,
                           uri,
                           widget->from_request.path_info,
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
        break;
    }

    widget->lazy.address = &widget->class->address;
    return;
}

const char *
widget_absolute_uri(pool_t pool, struct widget *widget,
                    const char *relative_uri, size_t relative_uri_length)
{
    return uri_absolute(pool, widget_address(pool, widget)->u.http->uri,
                        relative_uri, relative_uri_length);
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
                    bool focus,
                    const char *relative_uri, size_t relative_uri_length,
                    const char *frame, bool raw)
{
    const char *path;
    const char *new_uri;
    const char *args2;
    struct strref buffer;
    const struct strref *p;
    struct pool_mark mark;

    assert(focus || frame != NULL);
    assert(focus || (relative_uri == NULL && relative_uri_length == 0));
    assert(frame != NULL || !raw);

    path = widget_path(widget);
    if (path == NULL ||
        external_uri == NULL ||
        widget->class == &root_widget_class)
        return NULL;

    pool_mark(tpool, &mark);

    if (focus && relative_uri_length > 0) {
        assert(relative_uri != NULL);

        p = widget_relative_uri(tpool, widget,
                                relative_uri, relative_uri_length,
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
                          focus ? "focus" : NULL, path, strlen(path),
                          focus ? "path" : NULL,
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
