/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"

#include <string.h>
#include <assert.h>

const char *
widget_path(pool_t pool, const struct widget *widget)
{
    size_t length;
    const struct widget *w;
    char *path, *p;

    assert(widget != NULL);

    if (widget->parent == NULL)
        return NULL;

    for (w = widget, length = 0; w->parent != NULL; w = w->parent) {
        if (w->id == NULL)
            return NULL;
        length += strlen(w->id) + 1;
    }

    if (length == 0)
        return NULL;

    path = p_malloc(pool, length);
    p = path + length - 1;
    *p = 0;

    for (w = widget; w->parent != NULL; w = w->parent) {
        length = strlen(w->id);
        p -= length;
        memcpy(p, w->id, length);
        if (p > path) {
            --p;
            *p = '/';
        }
    }

    assert(p == path);

    return path;
}

const char *
widget_prefix(pool_t pool, const struct widget *widget)
{
    size_t length;
    const struct widget *w;
    char *path, *p;

    assert(widget != NULL);

    for (w = widget, length = 3; w->parent != NULL; w = w->parent) {
        if (w->id == NULL)
            return NULL;
        length += strlen(w->id) + 2;
    }

    path = p_malloc(pool, length);
    p = path + length - 1;
    *p = 0;

    for (w = widget; w->parent != NULL; w = w->parent) {
        p -= 2;
        p[0] = '_';
        p[1] = '_';
        length = strlen(w->id);
        p -= length;
        memcpy(p, w->id, length);
    }

    p -= 2;
    p[0] = '_';
    p[1] = '_';

    assert(p == path);

    return path;
}

void
widget_cancel(struct widget *widget)
{
    if (widget->body != NULL) {
        /* we are not going to consume the request body, so abort
           it */

        assert(!istream_has_handler(widget->body));

        istream_free(&widget->body);
    }
}
