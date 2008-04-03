/*
 * Embed a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "processor.h"
#include "widget.h"
#include "widget-registry.h"
#include "growing-buffer.h"
#include "js-generator.h"
#include "widget-stream.h"

#include <assert.h>
#include <string.h>

struct inline_widget {
    pool_t pool;
    struct processor_env *env;
    struct widget *widget;

    struct widget_stream *stream;
};

static void
inline_widget_set(struct inline_widget *iw);

static void
class_lookup_callback(const struct widget_class *class, void *_ctx)
{
    struct inline_widget *iw = _ctx;

    if (class != NULL) {
        iw->widget->class = class;
        inline_widget_set(iw);
    } else {
        async_ref_clear(istream_delayed_async(iw->stream->delayed));
        istream_free(&iw->stream->delayed);
        pool_unref(iw->pool);
    }
}

static void
embed_inline_widget(struct inline_widget *iw)
{
    unsigned options;

    if (iw->widget->class->old_style)
        options = PROCESSOR_FRAGMENT | PROCESSOR_JSCRIPT;
    else
        options = 0;

    widget_http_request(iw->pool, iw->widget,
                        iw->env, options,
                        &widget_stream_response_handler, iw->stream,
                        &iw->stream->async_ref);
}

static const char *
widget_frame_uri(pool_t pool, const struct processor_env *env,
                 struct widget *widget)
{
    if (widget->display == WIDGET_DISPLAY_EXTERNAL)
        /* XXX append google gadget preferences to query_string? */
        return widget->class->uri;

    return widget_external_uri(pool, env->external_uri, env->args,
                               widget, 0, NULL, 0,
                               1, 0);
}

/** generate IFRAME element; the client will perform a second request
    for the frame contents, see frame_widget_callback() */
istream_t
embed_iframe_widget(pool_t pool, const struct processor_env *env,
                    struct widget *widget)
{
    const char *uri, *prefix;
    struct growing_buffer *gb;

    uri = widget_frame_uri(pool, env, widget);
    prefix = widget_prefix(widget);
    /* XXX don't require prefix for WIDGET_DISPLAY_EXTERNAL */
    if (uri == NULL || prefix == NULL)
        return istream_string_new(pool, "[framed widget without id]"); /* XXX */

    gb = growing_buffer_new(pool, 512);
    growing_buffer_write_string(gb, "<iframe id=\"beng_iframe_");
    growing_buffer_write_string(gb, prefix);
    growing_buffer_write_string(gb, "\" "
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

static void
inline_widget_set(struct inline_widget *iw)
{
    struct widget *widget = iw->widget;

    widget_sync_session(widget);

    switch (widget->display) {
    case WIDGET_DISPLAY_INLINE:
        embed_inline_widget(iw);
        pool_unref(iw->pool);
        return;

    case WIDGET_DISPLAY_NONE:
        istream_delayed_set_eof(iw->stream->delayed);
        pool_unref(iw->pool);
        return;

    case WIDGET_DISPLAY_IFRAME:
    case WIDGET_DISPLAY_EXTERNAL:
        widget_cancel(widget);
        istream_delayed_set(iw->stream->delayed,
                            embed_iframe_widget(iw->pool, iw->env, iw->widget));
        pool_unref(iw->pool);
        return;
    }

    assert(0);
    istream_close(iw->stream->delayed);
}

istream_t
embed_widget_callback(pool_t pool, struct processor_env *env,
                      struct widget *widget)
{
    struct inline_widget *iw = p_malloc(pool, sizeof(*iw));

    assert(pool != NULL);
    assert(env != NULL);
    assert(widget != NULL);

    pool_ref(pool);

    iw->pool = pool;
    iw->env = env;
    iw->widget = widget;
    iw->stream = widget_stream_new(pool);

    if (widget->class == NULL)
        widget_class_lookup(env->pool, env->translate_stock, widget->class_name,
                            class_lookup_callback, iw, &iw->stream->async_ref);
    else
        inline_widget_set(iw);

    return iw->stream->delayed;
}
