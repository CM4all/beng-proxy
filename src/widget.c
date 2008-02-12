/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"

#include <string.h>
#include <assert.h>

void
widget_set_id(struct widget *widget, pool_t pool, const struct strref *id)
{
    const char *p;

    assert(id != NULL);
    assert(widget->parent != NULL);
    assert(!strref_is_empty(id));

    widget->id = strref_dup(pool, id);

    p = widget_path(pool, widget->parent);
    if (p != NULL)
        widget->lazy.path = p_strcat(pool, p, "/", widget->id, NULL);

    p = widget_prefix(pool, widget->parent);
    if (p != NULL)
        widget->lazy.prefix = p_strcat(pool, p, widget->id, "__", NULL);
}

void
widget_cancel(struct widget *widget)
{
    if (widget->from_request.body != NULL) {
        /* we are not going to consume the request body, so abort
           it */

        assert(!istream_has_handler(widget->from_request.body));

        istream_free(&widget->from_request.body);
    }
}
