/*
 * Determine the real URI of a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "uri.h"
#include "args.h"
#include "tpool.h"

#include <assert.h>

void
widget_determine_real_uri(pool_t pool, struct widget *widget)
{
    const char *uri;

    assert(widget != NULL);
    assert(widget->from_request.path_info != NULL);

    uri = widget->class->uri;

    if (!strref_is_empty(&widget->from_request.query_string))
        uri = p_strncat(pool,
                        uri, strlen(uri),
                        widget->from_request.path_info,
                        strlen(widget->from_request.path_info),
                        "?", (size_t)1,
                        widget->from_request.query_string.data,
                        widget->from_request.query_string.length,
                        NULL);
    else if (widget->from_request.path_info != NULL)
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

    widget->lazy.real_uri = uri;
}

const char *
widget_absolute_uri(pool_t pool, struct widget *widget,
                    const char *relative_uri, size_t relative_uri_length)
{
    if (widget->class == &root_widget_class)
        return NULL;

    return uri_absolute(pool, widget_real_uri(pool, widget),
                        relative_uri, relative_uri_length);
}

const char *
widget_proxy_uri(pool_t pool,
                 const struct parsed_uri *external_uri,
                 strmap_t args,
                 const struct widget *widget)
{
    const char *path, *args2;

    path = widget_path(widget);
    if (path == NULL)
        return NULL;

    args2 = args_format(pool, args,
                        "frame", path,
                        NULL, NULL,
                        NULL);

    return p_strncat(pool,
                     external_uri->base.data,
                     external_uri->base.length,
                     ";", (size_t)1,
                     args2, strlen(args2),
                     NULL);
}

const char *
widget_translation_uri(pool_t pool,
                       const struct parsed_uri *external_uri,
                       strmap_t args,
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

const char *
widget_external_uri(pool_t pool,
                    const struct parsed_uri *external_uri,
                    strmap_t args,
                    struct widget *widget,
                    const char *relative_uri, size_t relative_uri_length)
{
    const char *new_uri;
    const char *args2;
    struct strref s;
    const struct strref *p;
    struct pool_mark mark;

    if (relative_uri_length == 6 &&
        memcmp(relative_uri, ";proxy", 6) == 0)
        /* XXX this special URL syntax should be redesigned */
        return widget_proxy_uri(pool, external_uri, args, widget);

    if (relative_uri_length >= 11 &&
        memcmp(relative_uri, ";translate=", 11) == 0)
        /* XXX this special URL syntax should be redesigned */
        return widget_translation_uri(pool, external_uri, args,
                                      p_strndup(pool, relative_uri + 11,
                                                relative_uri_length - 11));

    if (widget->id == NULL ||
        external_uri == NULL ||
        widget->class == &root_widget_class)
        return widget_absolute_uri(pool, widget, relative_uri, relative_uri_length);

    pool_mark(tpool, &mark);

    new_uri = widget_absolute_uri(tpool, widget, relative_uri, relative_uri_length);
    if (new_uri == NULL)
        strref_set(&s, relative_uri, relative_uri_length);
    else
        strref_set_c(&s, new_uri);

    p = widget_class_relative_uri(widget->class, &s);
    if (p == NULL) {
        pool_rewind(tpool, &mark);
        return NULL;
    }

    /* the URI is relative to the widget's base URI.  Convert the URI
       into an absolute URI to the template page on this server and
       add the appropriate args. */
    args2 = args_format_n(tpool, args,
                          "focus", widget->id, strlen(widget->id),
                          "path", p->data, p->length,
                          NULL);

    new_uri = p_strncat(pool,
                        external_uri->base.data,
                        external_uri->base.length,
                        ";", (size_t)1,
                        args2, strlen(args2),
                        NULL);
    pool_rewind(tpool, &mark);

    return new_uri;
}
