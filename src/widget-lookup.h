/*
 * A handler class for looking up a widget in a container.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_LOOKUP_H
#define BENG_PROXY_WIDGET_LOOKUP_H

#include <glib.h>

struct widget;

struct widget_lookup_handler {
    void (*found)(struct widget *widget, void *ctx);
    void (*not_found)(void *ctx);
    void (*error)(GError *error, void *ctx);
};

#endif
