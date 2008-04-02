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

#include <assert.h>
#include <string.h>

static void
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    unsigned options;

    if (widget->class->old_style)
        options = PROCESSOR_FRAGMENT | PROCESSOR_JSCRIPT;
    else
        options = 0;

    widget_http_request(pool, widget,
                        env, options,
                        handler, handler_ctx, async_ref);
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

void
embed_widget_callback(pool_t pool, struct processor_env *env,
                      struct widget *widget,
                      const struct http_response_handler *handler,
                      void *handler_ctx,
                      struct async_operation_ref *async_ref)
{
    struct http_response_handler_ref handler_ref;
    istream_t istream = NULL;

    assert(pool != NULL);
    assert(env != NULL);
    assert(widget != NULL);

    if (widget->class == NULL) {
        widget_class_lookup(pool, env, widget,
                            handler, handler_ctx, async_ref);
        return;
    }

    widget_sync_session(widget);

    switch (widget->display) {
    case WIDGET_DISPLAY_INLINE:
        embed_inline_widget(pool, env, widget,
                            handler, handler_ctx, async_ref);
        return;

    case WIDGET_DISPLAY_NONE:
        http_response_handler_set(&handler_ref, handler, handler_ctx);
        http_response_handler_invoke_response(&handler_ref,
                                              HTTP_STATUS_NO_CONTENT,
                                              NULL, NULL);
        return;

    case WIDGET_DISPLAY_IFRAME:
    case WIDGET_DISPLAY_EXTERNAL:
        widget_cancel(widget);
        istream = embed_iframe_widget(pool, env, widget);
        break;
    }

    assert(istream != NULL);

    http_response_handler_set(&handler_ref, handler, handler_ctx);
    http_response_handler_invoke_response(&handler_ref, HTTP_STATUS_OK,
                                          NULL, istream);
}
