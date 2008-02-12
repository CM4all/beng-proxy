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

static void
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    embed_new(pool, widget,
              env, PROCESSOR_BODY | PROCESSOR_JSCRIPT,
              handler, handler_ctx, async_ref);
}

static const char *
widget_frame_uri(pool_t pool, const struct processor_env *env,
                 struct widget *widget)
{
    if (widget->display == WIDGET_DISPLAY_EXTERNAL)
        /* XXX append google gadget preferences to query_string? */
        return widget->class->uri;

    return widget_proxy_uri(pool, env->external_uri,
                            env->args, widget);
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
    prefix = widget_prefix(widget);
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

void
embed_widget_callback(pool_t pool, struct processor_env *env,
                      struct widget *widget,
                      const struct http_response_handler *handler,
                      void *handler_ctx,
                      struct async_operation_ref *async_ref)
{
    struct http_response_handler_ref handler_ref;
    istream_t istream;

    assert(pool != NULL);
    assert(env != NULL);
    assert(env->widget_callback == embed_widget_callback);
    assert(widget != NULL);

    (void)async_ref;

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    switch (widget->display) {
    case WIDGET_DISPLAY_INLINE:
        embed_inline_widget(pool, env, widget,
                            handler, handler_ctx, async_ref);
        return;

    case WIDGET_DISPLAY_IFRAME:
    case WIDGET_DISPLAY_EXTERNAL:
        widget_cancel(widget);
        istream = embed_iframe_widget(pool, env, widget);
        break;

    case WIDGET_DISPLAY_IMG:
        widget_cancel(widget);
        istream = embed_img_widget(pool, env, widget);
        break;
    }

    http_response_handler_invoke_response(&handler_ref, HTTP_STATUS_OK,
                                          NULL, istream);
}
