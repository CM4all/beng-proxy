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
widget_determine_real_uri(pool_t pool, struct widget *widget)
{
    const char *uri;

    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(widget->class->address.type == RESOURCE_ADDRESS_HTTP);
    assert(widget->class->address.u.http->uri != NULL);
    assert(widget->from_request.path_info != NULL);

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
    const char *absolute_uri;

    absolute_uri = widget_absolute_uri(pool, widget, relative_uri, relative_uri_length);
    if (absolute_uri == NULL)
        strref_set(buffer, relative_uri, relative_uri_length);
    else
        strref_set_c(buffer, absolute_uri);

    return widget_class_relative_uri(widget->class, buffer);
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

    if (focus) {
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
