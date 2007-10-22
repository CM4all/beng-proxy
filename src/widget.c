/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "session.h"

#include <string.h>
#include <assert.h>

const struct widget_class *
get_widget_class(pool_t pool, const char *uri)
{
    struct widget_class *wc = p_malloc(pool, sizeof(*wc));

    wc->uri = uri;

    return wc;
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


const struct widget_ref *
widget_ref_parse(pool_t pool, const char *_p)
{
    char *p, *slash;
    const struct widget_ref *root = NULL, **wr_p = &root;
    struct widget_ref *wr;

    if (_p == NULL || *_p == 0)
        return NULL;

    p = p_strdup(pool, _p);

    while ((slash = strchr(p, '/')) != NULL) {
        if (slash == p)
            continue;

        *slash = 0;
        wr = p_malloc(pool, sizeof(*wr));
        wr->next = NULL;
        wr->id = p;

        *wr_p = wr;
        wr_p = &wr->next;

        p = slash + 1;
    }

    if (*p != 0) {
        wr = p_malloc(pool, sizeof(*wr));
        wr->next = NULL;
        wr->id = p;
        *wr_p = wr;
    }

    return root;
}

static const struct widget_ref *
widget_widget_ref(pool_t pool, const struct widget *widget)
{
    /* XXX this is a waste of memory */
    struct widget_ref *wr = NULL, *next;

    assert(widget != NULL);

    while (widget->parent != NULL) {
        if (widget->id == NULL)
            return NULL;

        next = wr;
        wr = p_malloc(pool, sizeof(*wr));
        wr->next = next;
        wr->id = widget->id;

        widget = widget->parent;
    }

    return wr;
}

static int
widget_ref_compare2(const struct widget_ref *a, const struct widget_ref *b,
                    int partial_ok)
{
    while (a != NULL) {
        if (b == NULL || strcmp(a->id, b->id) != 0)
            return 0;

        a = a->next;
        b = b->next;
    }

    return partial_ok || b == NULL;
}

int
widget_ref_compare(pool_t pool, const struct widget *widget,
                   const struct widget_ref *ref, int partial_ok)
{
    const struct widget_ref *ref2;

    assert(widget != NULL);

    if (ref == NULL)
        return widget->parent == NULL;

    if (widget->parent == NULL)
        return partial_ok;

    ref2 = widget_widget_ref(pool, widget);
    if (ref2 == NULL)
        return 0;

    return widget_ref_compare2(ref2, ref, partial_ok);
}
