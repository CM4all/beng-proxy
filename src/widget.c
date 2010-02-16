/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "strref-pool.h"

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

    p = widget_path(widget->parent);
    if (p != NULL)
        widget->lazy.path = *p == 0
            ? widget->id
            : p_strcat(pool, p, "/", widget->id, NULL);

    p = widget_prefix(widget->parent);
    if (p != NULL)
        widget->lazy.prefix = p_strcat(pool, p, widget->id, "__", NULL);
}

struct widget *
widget_get_child(struct widget *widget, const char *id)
{
    struct widget *child;

    assert(widget != NULL);
    assert(id != NULL);

    for (child = (struct widget *)widget->children.next;
         child != (struct widget *)&widget->children;
         child = (struct widget *)child->siblings.next) {
        if (child->id != NULL && strcmp(child->id, id) == 0)
            return child;
    }

    return NULL;
}

bool
widget_check_host(const struct widget *widget, const char *host)
{
    assert(widget->class != NULL);

    return host == NULL ||
        (widget->class->host != NULL &&
         strcmp(host, widget->class->host) == 0);
}

bool
widget_check_recursion(const struct widget *widget)
{
    unsigned depth = 0;

    assert(widget != NULL);

    do {
        if (++depth >= 8)
            return true;

        widget = widget->parent;
    } while (widget != NULL);

    return false;
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
