/*
 * Embed a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "widget-http.h"
#include "processor.h"
#include "widget.h"
#include "widget-resolver.h"
#include "widget-stream.h"
#include "google-gadget.h"

#include <assert.h>

struct inline_widget {
    pool_t pool;
    struct processor_env *env;
    struct widget *widget;

    struct widget_stream *stream;
};

static void
inline_widget_set(struct inline_widget *iw)
{
    struct widget *widget = iw->widget;

    widget_sync_session(widget);

    switch (widget->class->type) {
    case WIDGET_TYPE_RAW:
    case WIDGET_TYPE_BENG:
        widget_http_request(iw->pool, iw->widget, iw->env,
                            &widget_stream_response_handler, iw->stream,
                            &iw->stream->async_ref);
        break;

    case WIDGET_TYPE_GOOGLE_GADGET:
        embed_google_gadget(iw->pool, iw->env, iw->widget,
                            &widget_stream_response_handler, iw->stream,
                            &iw->stream->async_ref);
        break;
    }

    pool_unref(iw->pool);
}

static void
class_lookup_callback(void *_ctx)
{
    struct inline_widget *iw = _ctx;

    if (iw->widget->class != NULL) {
        inline_widget_set(iw);
    } else {
        async_ref_clear(istream_delayed_async(iw->stream->delayed));
        istream_free(&iw->stream->delayed);
        pool_unref(iw->pool);
    }
}

istream_t
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget)
{
    struct inline_widget *iw = p_malloc(pool, sizeof(*iw));
    istream_t hold;

    assert(pool != NULL);
    assert(env != NULL);
    assert(widget != NULL);

    if (widget->display == WIDGET_DISPLAY_NONE)
        return NULL;

    pool_ref(pool);

    iw->pool = pool;
    iw->env = env;
    iw->widget = widget;
    iw->stream = widget_stream_new(pool);
    hold = istream_hold_new(pool, iw->stream->delayed);

    if (widget->class == NULL)
        widget_resolver_new(pool, pool, /* XXX which pool? */
                            widget,
                            env->translate_cache,
                            class_lookup_callback, iw, &iw->stream->async_ref);
    else
        inline_widget_set(iw);

    return hold;
}
