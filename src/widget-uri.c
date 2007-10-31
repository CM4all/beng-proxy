/*
 * Determine the real URI of a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "session.h"
#include "processor.h"
#include "uri.h"
#include "args.h"

#include <string.h>
#include <assert.h>

static void
connect_widget_session(const struct processor_env *env,
                       struct widget *widget, struct widget_session *ws)
{
    if (widget->from_request.focus ||
        widget->from_request.path_info != NULL || 
        widget->from_request.query_string ||
        widget->from_request.body) {
        /* reset state because we got a new state with this request */

        ws->path_info = p_strdup(ws->pool, widget->from_request.path_info);

        if (widget->from_request.query_string) {
            ws->query_string = p_strndup(ws->pool,
                                         env->external_uri->query,
                                         env->external_uri->query_length);
        } else {
            ws->query_string = NULL;
        }
    } else {
        /* copy state from session to widget */

        widget->from_request.path_info = ws->path_info;
    }
}

void
widget_determine_real_uri(pool_t pool, const struct processor_env *env,
                          struct widget *widget)
{
    struct widget_session *ws;
    const char *path_info = widget->path_info;

    widget->real_uri = widget->class->uri;

    /* are we focused? */

    if (widget->id != NULL && env->focus != NULL &&
        widget_ref_compare(pool, widget, env->focus, 0)) {
        /* we're in focus.  forward query string and request body. */
        widget->from_request.focus = 1;

        if (env->external_uri->query != NULL) {
            assert(env->external_uri->query_length > 0);
            widget->from_request.query_string = 1;
        }

        if (env->request_body != NULL)
            widget->from_request.body = 1;

        /* store query string in session */

        ws = widget_get_session(widget, 1);
    } else {
        /* get query string from session */

        ws = widget_get_session(widget, 0);
    }

    /* append path_info and query_string to widget->real_uri */

    if (ws == NULL) {
        if (widget->from_request.path_info != NULL)
            path_info = widget->from_request.path_info;
        else if (path_info == NULL)
            path_info = "";

        if (widget->from_request.query_string)
            widget->real_uri = p_strncat(pool,
                                         widget->real_uri, strlen(widget->real_uri),
                                         path_info, strlen(path_info),
                                         "?", 1,
                                         env->external_uri->query,
                                         env->external_uri->query_length,
                                         NULL);
        else if (*path_info != 0)
            widget->real_uri = p_strcat(pool,
                                        widget->real_uri,
                                        path_info,
                                        NULL);
    } else {
        connect_widget_session(env, widget, ws);

        if (ws->path_info != NULL)
            path_info = ws->path_info;
        else if (path_info == NULL)
            path_info = "";

        if (ws->query_string != NULL)
            widget->real_uri = p_strcat(pool,
                                        widget->real_uri,
                                        path_info,
                                        "?",
                                        ws->query_string,
                                        NULL);
        else if (*path_info != 0)
            widget->real_uri = p_strcat(pool,
                                        widget->real_uri,
                                        path_info,
                                        NULL);
    }

    if (widget->query_string != NULL)
        widget->real_uri = p_strcat(pool,
                                    widget->real_uri,
                                    strchr(widget->real_uri, '?') == NULL ? "?" : "&",
                                    widget->query_string,
                                    NULL);
}

const char *
widget_absolute_uri(pool_t pool, const struct widget *widget,
                    const char *relative_uri, size_t relative_uri_length)
{
    return uri_absolute(pool, widget->real_uri,
                        relative_uri, relative_uri_length);
}

const char *
widget_external_uri(pool_t pool,
                    const struct parsed_uri *external_uri,
                    strmap_t args,
                    const struct widget *widget,
                    const char *relative_uri, size_t relative_uri_length,
                    int focus, int remove_old_focus)
{
    const char *new_uri = widget_absolute_uri(pool, widget, relative_uri, relative_uri_length);
    const char *args2, *remove_key = NULL;

    if (widget->id == NULL ||
        external_uri == NULL ||
        widget->class == NULL)
        return new_uri;

    if (new_uri == NULL)
        new_uri = p_strndup(pool, relative_uri, relative_uri_length);

    new_uri = widget_class_relative_uri(widget->class, new_uri);
    if (new_uri == NULL)
        return NULL;

    if (!focus && memchr(relative_uri, '?', relative_uri_length) != NULL)
        /* switch on focus if the relative URI contains a query
           string */
        focus = 1;

    if (remove_old_focus || external_uri->query != NULL)
        remove_key = strmap_get(args, "focus");

    /* the URI is relative to the widget's base URI.  Convert the URI
       into an absolute URI to the template page on this server and
       add the appropriate args. */
    args2 = args_format(pool, args,
                        widget->id, new_uri,
                        "focus",
                        focus ? widget->id : NULL,
                        remove_key);

    return p_strncat(pool,
                     external_uri->base,
                     external_uri->base_length,
                     ";", 1,
                     args2, strlen(args2),
                     NULL);
}
