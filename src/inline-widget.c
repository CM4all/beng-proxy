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
#include "async.h"
#include "global.h"

#include <assert.h>

struct inline_widget {
    pool_t pool;
    struct processor_env *env;
    struct widget *widget;

    istream_t delayed;
};


static void
inline_widget_close(struct inline_widget *iw)
{
    /* clear the delayed async_ref object: we didn't provide an
       istream to the delayed object, and if we close it right now, it
       will trigger the async_abort(), unless we clear its
       async_ref */
    async_ref_clear(istream_delayed_async_ref(iw->delayed));

    istream_free(&iw->delayed);
}

/*
 * HTTP response handler
 *
 */

static void
inline_widget_response(__attr_unused http_status_t status,
                       __attr_unused struct strmap *headers,
                       istream_t body, void *ctx)
{
    struct inline_widget *iw = ctx;

    if (body == NULL)
        body = istream_null_new(iw->pool);

    istream_delayed_set(iw->delayed, body);

    if (istream_has_handler(iw->delayed))
        istream_read(iw->delayed);
}

static void
inline_widget_abort(void *ctx)
{
    struct inline_widget *iw = ctx;

    inline_widget_close(iw);
}

const struct http_response_handler inline_widget_response_handler = {
    .response = inline_widget_response,
    .abort = inline_widget_abort,
};


/*
 * internal
 *
 */

static void
inline_widget_set(struct inline_widget *iw)
{
    struct widget *widget = iw->widget;

    if (widget->class->stateful) {
        struct session *session = session_get(iw->env->session_id);
        if (session != NULL)
            widget_sync_session(widget, session);
    }

    widget_http_request(iw->pool, iw->widget, iw->env,
                        &inline_widget_response_handler, iw,
                        istream_delayed_async_ref(iw->delayed));
}


/*
 * Widget resolver callback
 *
 */

static void
class_lookup_callback(void *_ctx)
{
    struct inline_widget *iw = _ctx;

    if (iw->widget->class != NULL) {
        inline_widget_set(iw);
    } else {
        inline_widget_close(iw);
    }
}


/*
 * Constructor
 *
 */

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

    iw->pool = pool;
    iw->env = env;
    iw->widget = widget;
    iw->delayed = istream_delayed_new(pool);
    hold = istream_hold_new(pool, iw->delayed);

    if (widget->class == NULL)
        widget_resolver_new(pool, env->pool,
                            widget,
                            global_translate_cache,
                            class_lookup_callback, iw,
                            istream_delayed_async_ref(iw->delayed));
    else
        inline_widget_set(iw);

    return hold;
}
