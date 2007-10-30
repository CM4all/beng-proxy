/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"

#include <string.h>
#include <assert.h>

const struct widget_class *
get_widget_class(pool_t pool, const char *uri)
{
    struct widget_class *wc = p_malloc(pool, sizeof(*wc));

    wc->uri = uri;

    return wc;
}

const char *
widget_class_relative_uri(const struct widget_class *class, const char *uri)
{
    size_t class_uri_length;

    assert(class != NULL);
    assert(uri != NULL);

    if (class->uri == NULL)
        return NULL;

    class_uri_length = strlen(class->uri);

    if (strncmp(uri, class->uri, class_uri_length) != 0)
        return NULL;

    return class->uri + class_uri_length;
}

int
widget_class_includes_uri(const struct widget_class *class, const char *uri)
{
    assert(class != NULL);
    assert(uri != NULL);

    return class->uri != NULL &&
        strncmp(uri, class->uri, strlen(class->uri)) == 0;
}


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
