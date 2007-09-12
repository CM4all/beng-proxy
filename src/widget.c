/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"

const struct widget_class *
get_widget_class(pool_t pool, const char *uri)
{
    struct widget_class *wc = p_malloc(pool, sizeof(*wc));

    wc->uri = uri;

    return wc;
}
