/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-view.h"
#include "transformation.h"
#include "pool.h"

#include <string.h>

void
widget_view_init(struct widget_view *view)
{
    view->next = NULL;
    view->name = NULL;
    view->address.type = RESOURCE_ADDRESS_NONE;
    view->filter_4xx = false;
    view->inherited = false;
    view->transformation = NULL;
}

bool
widget_view_inherit_address(struct pool *pool, struct widget_view *view,
                            const struct resource_address *address)
{
    assert(view != NULL);
    assert(address != NULL);

    if (view->address.type != RESOURCE_ADDRESS_NONE ||
        address->type == RESOURCE_ADDRESS_NONE)
        return false;

    resource_address_copy(pool, &view->address, address);
    view->inherited = true;
    return true;
}

bool
widget_view_inherit_from(struct pool *pool, struct widget_view *dest,
                         const struct widget_view *src)
{
    if (widget_view_inherit_address(pool, dest, &src->address)) {
        dest->filter_4xx = src->filter_4xx;

        dest->request_header_forward = src->request_header_forward;
        dest->response_header_forward = src->response_header_forward;

        return true;
    } else
        return false;
}

const struct widget_view *
widget_view_lookup(const struct widget_view *view, const char *name)
{
    assert(view != NULL);
    assert(view->name == NULL);

    if (name == NULL || *name == 0)
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
    widget_view_init(dest);

    dest->name = src->name != NULL ? p_strdup(pool, src->name) : NULL;
    resource_address_copy(pool, &dest->address, &src->address);
    dest->filter_4xx = src->filter_4xx;
    dest->inherited = src->inherited;
    dest->transformation = transformation_dup_chain(pool, src->transformation);
    dest->request_header_forward = src->request_header_forward;
    dest->response_header_forward = src->response_header_forward;

    return dest;
}

struct widget_view *
widget_view_dup_chain(struct pool *pool, const struct widget_view *src)
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

bool
widget_view_is_expandable(const struct widget_view *view)
{
    return resource_address_is_expandable(&view->address) ||
        transformation_any_is_expandable(view->transformation);
}

bool
widget_view_any_is_expandable(const struct widget_view *view)
{
    while (view != NULL) {
        if (widget_view_is_expandable(view))
            return true;

        view = view->next;
    }

    return false;
}

bool
widget_view_expand(struct pool *pool, struct widget_view *view,
                   const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(view != NULL);
    assert(match_info != NULL);

    return resource_address_expand(pool, &view->address,
                                   match_info, error_r) &&
        transformation_expand_all(pool, view->transformation,
                                  match_info, error_r);
}

bool
widget_view_expand_all(struct pool *pool, struct widget_view *view,
                       const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(match_info != NULL);

    while (view != NULL) {
        if (!widget_view_expand(pool, view, match_info, error_r))
            return false;

        view = view->next;
    }

    return true;
}
