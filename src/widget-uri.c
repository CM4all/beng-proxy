/*
 * Determine the real URI of a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "session.h"
#include "processor.h"
#include "uri.h"

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
