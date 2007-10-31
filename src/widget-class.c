/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"

#include <string.h>
#include <assert.h>

const struct widget_class *
get_widget_class(pool_t pool, const char *uri)
{
    struct widget_class *wc = p_malloc(pool, sizeof(*wc));

    wc->uri = uri;

    return wc;
}

const char *
widget_class_relative_uri(const struct widget_class *class, const char *uri)
{
    size_t class_uri_length;

    assert(class != NULL);
    assert(uri != NULL);

    if (class->uri == NULL)
        return NULL;

    class_uri_length = strlen(class->uri);

    if (strncmp(uri, class->uri, class_uri_length) == 0)
        return uri + class_uri_length;

    /* special case: http://hostname without trailing slash */
    if (strncmp(uri, class->uri, class_uri_length - 1) == 0 &&
        uri[class_uri_length - 1] == 0 &&
        strrchr(uri, '/') < uri + 7)
        return "";

    return NULL;
}

int
widget_class_includes_uri(const struct widget_class *class, const char *uri)
{
    assert(class != NULL);
    assert(uri != NULL);

    return class->uri != NULL &&
        strncmp(uri, class->uri, strlen(class->uri)) == 0;
}
