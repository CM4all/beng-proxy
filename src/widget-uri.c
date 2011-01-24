/*
 * Determine the real URI of a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "widget-class.h"
#include "uri-relative.h"
#include "uri-parser.h"
#include "uri-edit.h"
#include "args.h"
#include "tpool.h"
#include "strref.h"
#include "strref-pool.h"
#include "uri-address.h"

#include <assert.h>

/**
 * Returns the "base" address of the widget, i.e. without the widget
 * parameters from the parent container.
 */
static const struct resource_address *
widget_base_address(pool_t pool, struct widget *widget, bool stateful)
{
    const struct resource_address *src = stateful
        ? widget_address(widget) : widget_stateless_address(widget);
    const char *uri;
    struct resource_address *dest;

    if (src->type != RESOURCE_ADDRESS_HTTP || widget->query_string == NULL)
        return src;

    uri = uri_delete_query_string(pool, src->u.http->uri,
                                  widget->query_string,
                                  strlen(widget->query_string));


    if (!strref_is_empty(&widget->from_request.query_string))
        uri = uri_delete_query_string(pool, src->u.http->uri,
                                      widget->from_request.query_string.data,
                                      widget->from_request.query_string.length);

    if (uri == src->u.http->uri)
        return src;

    dest = resource_address_dup(pool, src);
    dest->u.http->uri = uri;
    return dest;
}

const struct resource_address *
widget_determine_address(const struct widget *widget, bool stateful)
{
    pool_t pool = widget->pool;
    const char *path_info, *uri;
    struct resource_address *address;

    assert(widget != NULL);
    assert(widget->class != NULL);

    path_info = stateful ? widget_get_path_info(widget) : widget->path_info;
    assert(path_info != NULL);

    switch (widget->class->address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        break;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_AJP:
        assert(widget->class->address.u.http->uri != NULL);

        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        uri = widget->class->address.u.http->uri;

        if (*path_info != 0)
            uri = p_strcat(pool, uri, path_info, NULL);

        if (widget->query_string != NULL)
            uri = uri_insert_query_string(pool, uri,
                                          widget->query_string);

        if (stateful && !strref_is_empty(&widget->from_request.query_string))
            uri = uri_append_query_string_n(pool, uri,
                                            widget->from_request.query_string.data,
                                            widget->from_request.query_string.length);

        address = resource_address_dup(pool, &widget->class->address);
        address->u.http->uri = uri;
        return address;

    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        if ((!stateful ||
             strref_is_empty(&widget->from_request.query_string)) &&
            *path_info == 0 &&
            widget->query_string == NULL)
            break;

        address = resource_address_dup(pool, &widget->class->address);

        if (*path_info != 0)
            address->u.cgi.path_info = path_info;

        if (!stateful)
            address->u.cgi.query_string = widget->query_string;
        else if (strref_is_empty(&widget->from_request.query_string))
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

        return address;
    }

    return &widget->class->address;
}

const char *
widget_absolute_uri(pool_t pool, struct widget *widget, bool stateful,
                    const struct strref *relative_uri)
{
    const char *base, *uri;

    assert(widget_address(widget)->type == RESOURCE_ADDRESS_HTTP);

    base = (stateful ? widget_address(widget)
            : widget_stateless_address(widget))->u.http->uri;
    if (relative_uri == NULL)
        return base;

    uri = uri_absolute(pool, base, relative_uri->data, relative_uri->length);
    if (!strref_is_empty(relative_uri) && widget->query_string != NULL)
        /* the relative_uri is non-empty, and uri_absolute() has
           removed the query string: re-add the configured query
           string */
        uri = uri_insert_query_string(pool, uri, widget->query_string);

    return uri;
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

const struct strref *
widget_relative_uri(pool_t pool, struct widget *widget, bool stateful,
                    const char *relative_uri, size_t relative_uri_length,
                    struct strref *buffer)
{
    struct resource_address address_buffer;
    const struct resource_address *address;

    address = resource_address_apply(pool, widget_base_address(pool, widget, stateful),
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
                    struct widget *widget, bool stateful,
                    const struct strref *relative_uri,
                    const char *frame, bool raw)
{
    const char *path;
    const char *qmark, *args2, *new_uri;
    struct strref buffer, query_string;
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
        p = widget_relative_uri(tpool, widget, stateful,
                                relative_uri->data, relative_uri->length,
                                &buffer);
        if (p == NULL) {
            pool_rewind(tpool, &mark);
            return NULL;
        }
    } else
        p = NULL;

    if (p != NULL && strref_chr(relative_uri, '?') == 0 &&
        widget->query_string != NULL) {
        /* no query string in relative_uri: if there is one in the new
           URI, check it and remove the configured parameters */
        const char *uri =
            uri_delete_query_string(tpool, strref_dup(tpool, p),
                                    widget->query_string,
                                    strlen(widget->query_string));
        strref_set_c(&buffer, uri);
        p = &buffer;
    }

    if (p != NULL && (qmark = memchr(p->data, '?', p->length)) != NULL) {
        /* separate query_string from path_info */
        strref_set2(&query_string, qmark, strref_end(p));
        strref_set2(&buffer, p->data, qmark);
        p = &buffer;
    } else {
        strref_null(&query_string);
    }

    /* the URI is relative to the widget's base URI.  Convert the URI
       into an absolute URI to the template page on this server and
       add the appropriate args. */
    args2 = args_format_n(tpool, args,
                          "focus", path, strlen(path),
                          p == NULL ? NULL : "path",
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
                        query_string.data, query_string.length,
                        NULL);

    pool_rewind(tpool, &mark);
    return new_uri;
}
