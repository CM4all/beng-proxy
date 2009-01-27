/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "transformation.h"

const struct widget_class root_widget_class = {
    .address = {
        .type = RESOURCE_ADDRESS_NONE,
    },
    .stateful = false,
};

bool
widget_class_is_container(const struct widget_class *class,
                          const char *view_name)
{
    const struct transformation_view *view;

    assert(class != &root_widget_class);

    view = transformation_view_lookup(class->views, view_name);
    if (view == NULL)
        /* shouldn't happen, but may not be checked up to now */
        return false;

    return transformation_is_container(view->transformation);
}

