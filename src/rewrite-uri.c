/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rewrite-uri.h"
#include "widget.h"
#include "widget-stream.h"
#include "widget-resolver.h"
#include "strref-pool.h"

static const char *
current_frame(const struct widget *widget)
{
    do {
        if (widget->from_request.proxy)
            return widget_path(widget);

        widget = widget->parent;
    } while (widget != NULL);

    return NULL;
}

static const char *
do_rewrite_widget_uri(pool_t pool, const struct parsed_uri *external_uri,
                      struct strmap *args, struct widget *widget,
                      const struct strref *value,
                      enum uri_mode mode)
{
    const char *frame = NULL;
    bool raw = false;

    switch (mode) {
    case URI_MODE_DIRECT:
        if (widget->class->address.type != RESOURCE_ADDRESS_HTTP)
            /* the browser can only contact HTTP widgets directly */
            return NULL;

        return widget_absolute_uri(pool, widget,
                                   value->data, value->length);

    case URI_MODE_FOCUS:
        frame = current_frame(widget);
        break;

    case URI_MODE_PARTIAL:
        frame = widget_path(widget);
        break;

    case URI_MODE_PROXY:
        frame = widget_path(widget);
        raw = true;
        break;
    }

    return widget_external_uri(pool, external_uri, args,
                               widget,
                               true,
                               value->data, value->length,
                               frame, raw);
}

struct rewrite_widget_uri {
    pool_t pool;
    const struct parsed_uri *external_uri;
    struct strmap *args;
    struct widget *widget;
    session_id_t session_id;
    struct strref value;
    enum uri_mode mode;
    struct widget_stream *stream;
};

static void
class_lookup_callback(void *ctx)
{
    struct rewrite_widget_uri *rwu = ctx;

    if (rwu->widget->class != NULL) {
        struct session *session;
        const char *uri;

        session = session_get(rwu->session_id);
        if (session != NULL)
            widget_sync_session(rwu->widget, session);

        uri = do_rewrite_widget_uri(rwu->pool, rwu->external_uri, rwu->args,
                                    rwu->widget, &rwu->value, rwu->mode);
        if (uri != NULL)
            strref_set_c(&rwu->value, uri);
    }

    istream_delayed_set(rwu->stream->delayed,
                        istream_memory_new(rwu->pool,
                                           rwu->value.data,
                                           rwu->value.length));
}

istream_t
rewrite_widget_uri(pool_t pool, pool_t widget_pool,
                   struct tcache *translate_cache,
                   const struct parsed_uri *external_uri,
                   struct strmap *args, struct widget *widget,
                   session_id_t session_id,
                   const struct strref *value,
                   enum uri_mode mode)
{
    const char *uri;



    if (widget->class != NULL) {
        uri = do_rewrite_widget_uri(pool, external_uri, args, widget, value, mode);
        if (uri == NULL)
            return NULL;

        return istream_string_new(pool, uri);
    } else {
        struct rewrite_widget_uri *rwu = p_malloc(pool, sizeof(*rwu));
        istream_t hold;

        rwu->pool = pool;
        rwu->external_uri = external_uri;
        rwu->args = args;
        rwu->widget = widget;
        rwu->session_id = session_id;
        strref_set_dup(pool, &rwu->value, value);
        rwu->mode = mode;
        rwu->stream = widget_stream_new(pool);
        hold = istream_hold_new(pool, rwu->stream->delayed);

        widget_resolver_new(pool, widget_pool,
                            widget,
                            translate_cache,
                            class_lookup_callback, rwu,
                            &rwu->stream->async_ref);
        return hold;
    }
}
