/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

/* XXX remove memrchr() usage */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include <features.h>
#endif

#include "widget.h"

#include <string.h>
#include <assert.h>

const struct widget_class root_widget_class = {
    .uri = NULL,
    .type = WIDGET_TYPE_BENG,
    .is_container = 1,
    .old_style = 1,
};

const struct widget_class *
get_widget_class(pool_t pool, const char *uri, enum widget_type type)
{
    struct widget_class *wc = p_malloc(pool, sizeof(*wc));

    wc->uri = uri;
    wc->type = type;
    wc->is_container = type == WIDGET_TYPE_BENG;
    wc->old_style =  1;

    return wc;
}

const struct strref *
widget_class_relative_uri(const struct widget_class *class,
                          struct strref *uri)
{
    size_t class_uri_length;

    assert(class != NULL);
    assert(uri != NULL);

    if (class->uri == NULL)
        return NULL;

    class_uri_length = strlen(class->uri);

    if (uri->length >= class_uri_length &&
        memcmp(uri->data, class->uri, class_uri_length) == 0) {
        strref_skip(uri, class_uri_length);
        return uri;
    }

    /* special case: http://hostname without trailing slash */
    if (uri->length == class_uri_length - 1 &&
        memcmp(uri->data, class->uri, uri->length) &&
        (const char*)memrchr(uri->data, '/', uri->length) < uri->data + 7) {
        strref_clear(uri);
        return uri;
    }

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
