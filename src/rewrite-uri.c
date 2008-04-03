/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rewrite-uri.h"
#include "widget.h"

static const char *
do_rewrite_widget_uri(pool_t pool, const struct parsed_uri *external_uri,
                      struct strmap *args, struct widget *widget,
                      const struct strref *value,
                      enum uri_mode mode)
{
    int frame, raw;

    switch (mode) {
    case URI_MODE_DIRECT:
        return widget_absolute_uri(pool, widget,
                                   value->data, value->length);

    case URI_MODE_FULL:
        frame = raw = 0;
        break;

    case URI_MODE_PARTIAL:
        frame = 1;
        raw = 0;
        break;

    case URI_MODE_PROXY:
        frame = raw = 1;
        break;
    }

    return widget_external_uri(pool, external_uri, args,
                               widget,
                               1,
                               value->data, value->length,
                               frame, raw);
}

istream_t
rewrite_widget_uri(pool_t pool, const struct parsed_uri *external_uri,
                   struct strmap *args, struct widget *widget,
                   const struct strref *value,
                   enum uri_mode mode)
{
    const char *uri;

    uri = do_rewrite_widget_uri(pool, external_uri, args, widget, value, mode);
    if (uri == NULL)
        return NULL;

    return istream_string_new(pool, uri);
}
