/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rewrite-uri.h"
#include "widget.h"
#include "widget-stream.h"
#include "widget-registry.h"

static const char *
do_rewrite_widget_uri(pool_t pool, const struct parsed_uri *external_uri,
                      struct strmap *args, struct widget *widget,
                      const struct strref *value,
                      enum uri_mode mode)
{
    const char *frame;
    int raw;

    switch (mode) {
    case URI_MODE_DIRECT:
        return widget_absolute_uri(pool, widget,
                                   value->data, value->length);

    case URI_MODE_FULL:
        frame = NULL;
        raw = 0;
        break;

    case URI_MODE_PARTIAL:
        frame = widget_path(widget);
        raw = 0;
        break;

    case URI_MODE_PROXY:
        frame = widget_path(widget);
        raw = 1;
        break;
    }

    return widget_external_uri(pool, external_uri, args,
                               widget,
                               1,
                               value->data, value->length,
                               frame, raw);
}

struct rewrite_widget_uri {
    pool_t pool;
    const struct parsed_uri *external_uri;
    struct strmap *args;
    struct widget *widget;
    struct strref value;
    enum uri_mode mode;
    struct widget_stream *stream;
};

static void
class_lookup_callback(const struct widget_class *class, void *ctx)
{
    struct rewrite_widget_uri *rwu = ctx;

    if (class != NULL) {
        const char *uri;

        rwu->widget->class = class;
        widget_sync_session(rwu->widget);

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
rewrite_widget_uri(pool_t pool, struct stock *translate_stock,
                   const struct parsed_uri *external_uri,
                   struct strmap *args, struct widget *widget,
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
        strref_set_dup(pool, &rwu->value, value);
        rwu->mode = mode;
        rwu->stream = widget_stream_new(pool);
        hold = istream_hold_new(pool, rwu->stream->delayed);

        widget_class_lookup(pool, translate_stock,
                            widget->class_name,
                            class_lookup_callback, rwu,
                            &rwu->stream->async_ref);
        return hold;
    }
}
