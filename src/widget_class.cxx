/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_class.hxx"

const WidgetClass root_widget_class = {
    .views = {
        .address = ResourceAddress(RESOURCE_ADDRESS_NONE),
    },
    .stateful = false,
    .container_groups = StringSet(),
};
