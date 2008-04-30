/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "uri-address.h"

#include <string.h>
#include <assert.h>

const struct widget_class root_widget_class = {
    .address = NULL,
    .type = WIDGET_TYPE_BENG,
    .is_container = true,
    .old_style = true,
};

const struct widget_class *
get_widget_class(pool_t pool, const char *uri, enum widget_type type)
{
    struct widget_class *wc = p_malloc(pool, sizeof(*wc));

    wc->address = uri_address_new(pool, uri);
    wc->type = type;
    wc->is_container = type == WIDGET_TYPE_BENG;
    wc->old_style = true;

    return wc;
}

const struct strref *
widget_class_relative_uri(const struct widget_class *class,
                          struct strref *uri)
{
    const char *class_uri;
    size_t class_uri_length;

    assert(class != NULL);
    assert(uri != NULL);

    if (class->address == NULL)
        return NULL;

    assert(class->address->uri != NULL);

    class_uri = class->address->uri;
    class_uri_length = strlen(class_uri);

    if (uri->length >= class_uri_length &&
        memcmp(uri->data, class_uri, class_uri_length) == 0) {
        strref_skip(uri, class_uri_length);
        return uri;
    }

    /* special case: http://hostname without trailing slash */
    if (uri->length == class_uri_length - 1 &&
        memcmp(uri->data, class_uri, uri->length) &&
        memchr(uri->data + 7, '/', uri->length - 7) == NULL) {
        strref_clear(uri);
        return uri;
    }

    return NULL;
}
