/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "uri-address.h"
#include "uri-relative.h"

#include <string.h>
#include <assert.h>

const struct widget_class root_widget_class = {
    .address = {
        .type = RESOURCE_ADDRESS_NONE,
    },
    .type = WIDGET_TYPE_BENG,
    .is_container = true,
};

const struct strref *
widget_class_relative_uri(const struct widget_class *class,
                          struct strref *uri)
{
    struct strref class_uri;

    assert(class != NULL);
    assert(uri != NULL);

    if (class->address.type != RESOURCE_ADDRESS_HTTP)
        return NULL;

    assert(class->address.u.http != NULL);

    strref_set_c(&class_uri, class->address.u.http->uri);
    return uri_relative(&class_uri, uri);
}
