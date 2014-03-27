/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_view.hxx"
#include "transformation.hxx"
#include "pool.h"

#include <string.h>

void
widget_view_init(struct widget_view *view)
{
    view->next = nullptr;
    view->name = nullptr;
    view->address.type = RESOURCE_ADDRESS_NONE;
    view->filter_4xx = false;
    view->inherited = false;
    view->transformation = nullptr;

    view->request_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
        },
    };

    view->response_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
        },
    };
}

bool
widget_view_inherit_address(struct pool *pool, struct widget_view *view,
                            const struct resource_address *address)
{
    assert(view != nullptr);
    assert(address != nullptr);

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
    assert(view != nullptr);
    assert(view->name == nullptr);

    if (name == nullptr || *name == 0)
        /* the default view has no name */
        return view;

    for (view = view->next; view != nullptr; view = view->next) {
        assert(view->name != nullptr);

        if (strcmp(view->name, name) == 0)
            return view;
    }

    return nullptr;
}

bool
widget_view_has_processor(const struct widget_view *view)
{
    return view->transformation->HasProcessor();
}

bool
widget_view_is_container(const struct widget_view *view)
{
    return view->transformation->IsContainer();
}

static struct widget_view *
widget_view_dup(struct pool *pool, const struct widget_view *src)
{
    auto dest = NewFromPool<struct widget_view>(pool);
    widget_view_init(dest);

    dest->name = src->name != nullptr ? p_strdup(pool, src->name) : nullptr;
    resource_address_copy(pool, &dest->address, &src->address);
    dest->filter_4xx = src->filter_4xx;
    dest->inherited = src->inherited;
    dest->transformation = src->transformation->DupChain(pool);
    dest->request_header_forward = src->request_header_forward;
    dest->response_header_forward = src->response_header_forward;

    return dest;
}

struct widget_view *
widget_view_dup_chain(struct pool *pool, const struct widget_view *src)
{
    struct widget_view *dest = nullptr, **tail_p = &dest;

    assert(src != nullptr);
    assert(src->name == nullptr);

    for (; src != nullptr; src = src->next) {
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
        view->transformation->IsChainExpandable();
}

bool
widget_view_any_is_expandable(const struct widget_view *view)
{
    while (view != nullptr) {
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
    assert(pool != nullptr);
    assert(view != nullptr);
    assert(match_info != nullptr);

    return resource_address_expand(pool, &view->address,
                                   match_info, error_r) &&
        view->transformation->ExpandChain(pool, match_info, error_r);
}

bool
widget_view_expand_all(struct pool *pool, struct widget_view *view,
                       const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != nullptr);
    assert(match_info != nullptr);

    while (view != nullptr) {
        if (!widget_view_expand(pool, view, match_info, error_r))
            return false;

        view = view->next;
    }

    return true;
}
