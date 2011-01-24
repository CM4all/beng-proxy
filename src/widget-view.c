/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-view.h"
#include "transformation.h"
#include "pool.h"

#include <string.h>

const struct widget_view *
widget_view_lookup(const struct widget_view *view, const char *name)
{
    assert(view != NULL);
    assert(view->name == NULL);

    if (name == NULL)
        /* the default view has no name */
        return view;

    for (view = view->next; view != NULL; view = view->next) {
        assert(view->name != NULL);

        if (strcmp(view->name, name) == 0)
            return view;
    }

    return NULL;
}

static struct widget_view *
widget_view_dup(struct pool *pool, const struct widget_view *src)
{
    struct widget_view *dest = p_malloc(pool, sizeof(*dest));

    dest->next = NULL;
    dest->name = src->name != NULL ? p_strdup(pool, src->name) : NULL;
    dest->transformation = transformation_dup_chain(pool, src->transformation);

    return dest;
}

struct widget_view *
widget_view_dup_chain(pool_t pool, const struct widget_view *src)
{
    struct widget_view *dest = NULL, **tail_p = &dest;

    assert(src != NULL);
    assert(src->name == NULL);

    for (; src != NULL; src = src->next) {
        struct widget_view *p = widget_view_dup(pool, src);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;
}
