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
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget)
{
    istream_t request_body = NULL;

    if (widget->from_request.body) {
        assert(env->request_body != NULL);

        request_body = env->request_body;
        /* XXX what if there is no stream handler? or two? */
    }

    return embed_new(pool,
                     widget->from_request.body, widget->real_uri, request_body,
                     widget,
                     env, PROCESSOR_BODY | PROCESSOR_JSCRIPT);
}

static const char *
widget_frame_uri(pool_t pool, const struct processor_env *env,
                 struct widget *widget)
{
    const char *path;
    char session_id_buffer[9];

    path = widget_path(pool, widget);
    if (path == NULL)
        return NULL;

    session_id_format(session_id_buffer, env->session->id);

    return p_strcat(pool,
                    strref_dup(pool, &env->external_uri->base),
                    ";session=", session_id_buffer,
                    "&frame=", path,
                    "&", widget->id, "=",
                    widget->from_request.path_info == NULL ? "" : widget->from_request.path_info,
                    NULL);
}

/** generate IFRAME element; the client will perform a second request
    for the frame contents, see frame_widget_callback() */
static istream_t
embed_iframe_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    const char *uri, *iframe;

    uri = widget_frame_uri(pool, env, widget);
    if (uri == NULL)
        return istream_string_new(pool, "[framed widget without id]"); /* XXX */

    iframe = p_strcat(pool, "<iframe "
                      "width='100%' height='100%' "
                      "frameborder='0' marginheight='0' marginwidth='0' "
                      "scrolling='no' "
                      "src='", uri, "'></iframe>",
                      NULL);
    return istream_string_new(pool, iframe);
}

/** generate IMG element */
static istream_t
embed_img_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    const char *uri, *html;

    uri = widget_frame_uri(pool, env, widget);
    if (uri == NULL)
        return istream_string_new(pool, "[framed widget without id]"); /* XXX */

    html = p_strcat(pool, "<img src='", uri, "'></img>", NULL);
    return istream_string_new(pool, html);
}

istream_t
embed_widget_callback(pool_t pool, struct processor_env *env,
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
