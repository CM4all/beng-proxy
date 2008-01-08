/*
 * Embed a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "processor.h"
#include "widget.h"
#include "growing-buffer.h"
#include "js-generator.h"

#include <assert.h>
#include <string.h>

static istream_t
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget)
{
    return embed_new(pool,
                     widget->from_request.method, widget->real_uri,
                     widget->from_request.body,
                     widget,
                     env, PROCESSOR_BODY | PROCESSOR_JSCRIPT);
}

static const char *
widget_frame_uri(pool_t pool, const struct processor_env *env,
                 struct widget *widget)
{
    return widget_proxy_uri(pool, env->external_uri,
                            env->args, widget);
}

/** generate IFRAME element; the client will perform a second request
    for the frame contents, see frame_widget_callback() */
static istream_t
embed_iframe_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    const char *uri, *prefix;
    struct growing_buffer *gb;

    uri = widget_frame_uri(pool, env, widget);
    prefix = widget_prefix(pool, widget);
    if (uri == NULL || prefix == NULL)
        return istream_string_new(pool, "[framed widget without id]"); /* XXX */

    gb = growing_buffer_new(pool, 512);
    growing_buffer_write_string(gb, "<iframe id=\"beng_iframe_");
    growing_buffer_write_string(gb, prefix);
    growing_buffer_write_string(gb, "\""
                                "width='100%' height='100%' "
                                "frameborder='0' marginheight='0' marginwidth='0' "
                                "scrolling='no' "
                                "src='");
    growing_buffer_write_string(gb, uri);
    growing_buffer_write_string(gb, "'></iframe>");

    growing_buffer_write_string(gb, "<script type=\"text/javascript\">\n");
    js_generate_widget(gb, widget, pool);
    growing_buffer_write_string(gb, "</script>\n");

    return growing_buffer_istream(gb);
}

/** generate IMG element */
static istream_t
embed_img_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    const char *uri, *prefix;
    struct growing_buffer *gb;

    uri = widget_frame_uri(pool, env, widget);
    prefix = widget_prefix(pool, widget);
    if (uri == NULL || prefix == NULL)
        return istream_string_new(pool, "[framed widget without id]"); /* XXX */

    gb = growing_buffer_new(pool, 512);
    growing_buffer_write_string(gb, "<img id=\"beng_img_");
    growing_buffer_write_string(gb, prefix);
    growing_buffer_write_string(gb, "\" src=\"");
    growing_buffer_write_string(gb, uri);
    growing_buffer_write_string(gb, "\"></img>");

    growing_buffer_write_string(gb, "<script type=\"text/javascript\">\n");
    js_generate_widget(gb, widget, pool);
    growing_buffer_write_string(gb, "</script>\n");

    return growing_buffer_istream(gb);
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
        widget_cancel(widget);
        return embed_iframe_widget(pool, env, widget);

    case WIDGET_DISPLAY_IMG:
        widget_cancel(widget);
        return embed_img_widget(pool, env, widget);
    }

    assert(0);
    return NULL;
}
