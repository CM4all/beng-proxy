/*
 * Widget class functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"

const struct widget_class root_widget_class = {
    .address = {
        .type = RESOURCE_ADDRESS_NONE,
    },
    .is_container = true,
    .stateful = false,
};
