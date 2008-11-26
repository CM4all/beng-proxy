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
widget_class_is_container(const struct widget_class *class)
{
    assert(class != &root_widget_class);

    return transformation_is_container(class->transformation);
}

