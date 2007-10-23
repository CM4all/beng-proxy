/*
 * Embed a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "uri.h"
#include "processor.h"
#include "widget.h"
#include "session.h"

#include <assert.h>
#include <string.h>

static istream_t
embed_inline_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    http_method_t method = HTTP_METHOD_GET;
    off_t request_content_length = 0;
    istream_t request_body = NULL;
    struct widget_session *ws;

    if (widget->id != NULL && env->focus != NULL &&
        (env->external_uri->query != NULL || env->request_body != NULL) &&
        widget_ref_compare(pool, widget, env->focus, 0)) {
        /* we're in focus.  forward query string and request body. */
        widget->real_uri = p_strncat(pool,
                                     widget->real_uri, strlen(widget->real_uri),
                                     "?", 1,
                                     env->external_uri->query,
                                     env->external_uri->query_length,
                                     NULL);

        if (env->request_body != NULL) {
            widget->from_request.body = 1;
            method = HTTP_METHOD_POST; /* XXX which method? */
            request_content_length = env->request_content_length;
            request_body = istream_hold_new(pool, env->request_body);
            /* XXX what if there is no stream handler? or two? */
        }

        /* store query string in session */

        ws = widget_get_session(widget, 1);
        if (ws != NULL) {
            if (env->external_uri->query == NULL)
                ws->query_string = NULL;
            else
                ws->query_string = p_strndup(ws->pool,
                                             env->external_uri->query,
                                             env->external_uri->query_length);
        }
    } else {
        /* get query string from session */

        ws = widget_get_session(widget, 0);
        if (ws != NULL && ws->query_string != NULL)
            widget->real_uri = p_strcat(pool,
                                        widget->real_uri,
                                        "?",
                                        ws->query_string,
                                        NULL);
    }

    if (widget->query_string != NULL)
        widget->real_uri = p_strcat(pool,
                                    widget->real_uri,
                                    strchr(widget->real_uri, '?') == NULL ? "?" : "&",
                                    widget->query_string,
                                    NULL);

    return embed_new(pool,
                     method, widget->real_uri,
                     request_content_length, request_body,
                     widget,
                     env, PROCESSOR_BODY);
}

/** generate IFRAME element; the client will perform a second request
    for the frame contents, see frame_widget_callback() */
static istream_t
embed_iframe_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    const char *path, *iframe;
    char session_id_buffer[9];

    path = widget_path(pool, widget);
    if (path == NULL)
        return istream_string_new(pool, "[framed widget without id]"); /* XXX */

    session_id_format(session_id_buffer, env->session->id);

    iframe = p_strcat(pool, "<iframe "
                      "width='100%' height='100%' "
                      "frameborder='0' marginheight='0' marginwidth='0' "
                      "scrolling='no' "
                      "src='",
                      env->external_uri->base,
                      ";session=", session_id_buffer,
                      "&frame=", path,
                      "&", widget->id, "=",
                      widget->from_request.path_info == NULL ? "" : widget->from_request.path_info,
                      "'></iframe>",
                      NULL);
    return istream_string_new(pool, iframe);
}

/** generate IMG element */
static istream_t
embed_img_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    const char *path, *html;
    char session_id_buffer[9];

    path = widget_path(pool, widget);
    if (path == NULL)
        return istream_string_new(pool, "[framed widget without id]"); /* XXX */

    session_id_format(session_id_buffer, env->session->id);

    html = p_strcat(pool, "<img src='",
                    env->external_uri->base,
                    ";session=", session_id_buffer,
                    "&frame=", path,
                    "&", widget->id, "=",
                    widget->from_request.path_info == NULL ? "" : widget->from_request.path_info,
                    "'></img>",
                    NULL);
    return istream_string_new(pool, html);
}

istream_t
embed_widget_callback(pool_t pool, const struct processor_env *env,
                      struct widget *widget)
{
    assert(pool != NULL);
    assert(env != NULL);
    assert(env->widget_callback == embed_widget_callback);
    assert(widget != NULL);

    switch (widget->display) {
    case WIDGET_DISPLAY_INLINE:
        return embed_inline_widget(pool, env, widget);

    case WIDGET_DISPLAY_IFRAME:
        return embed_iframe_widget(pool, env, widget);

    case WIDGET_DISPLAY_IMG:
        return embed_img_widget(pool, env, widget);
    }

    assert(0);
    return NULL;
}
