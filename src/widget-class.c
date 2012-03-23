/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-class.h"
#include "widget-view.h"

const struct widget_class root_widget_class = {
    .views = {
        .address = {
            .type = RESOURCE_ADDRESS_NONE,
        },
    },
    .stateful = false,
};
