/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_view.hxx"
#include "transformation.hxx"
#include "pool.hxx"

#include <string.h>

void
widget_view::Init()
{
    next = nullptr;
    name = nullptr;
    address.type = RESOURCE_ADDRESS_NONE;
    filter_4xx = false;
    inherited = false;
    transformation = nullptr;

    request_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
        },
    };

    response_header_forward = (struct header_forward_settings){
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
widget_view::InheritAddress(struct pool &pool,
                            const struct resource_address &src)
{
    if (address.type != RESOURCE_ADDRESS_NONE ||
        src.type == RESOURCE_ADDRESS_NONE)
        return false;

    resource_address_copy(&pool, &address, &src);
    inherited = true;
    return true;
}

bool
widget_view::InheritFrom(struct pool &pool, const struct widget_view &src)
{
    if (InheritAddress(pool, src.address)) {
        filter_4xx = src.filter_4xx;

        request_header_forward = src.request_header_forward;
        response_header_forward = src.response_header_forward;

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
widget_view::HasProcessor() const
{
    return transformation->HasProcessor();
}

bool
widget_view::IsContainer() const
{
    return transformation->IsContainer();
}

static struct widget_view *
widget_view_dup(struct pool *pool, const struct widget_view *src)
{
    auto dest = NewFromPool<struct widget_view>(pool);
    dest->Init();

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
widget_view::IsExpandable() const
{
    return resource_address_is_expandable(&address) ||
        transformation->IsChainExpandable();
}

bool
widget_view_any_is_expandable(const struct widget_view *view)
{
    while (view != nullptr) {
        if (view->IsExpandable())
            return true;

        view = view->next;
    }

    return false;
}

bool
widget_view::Expand(struct pool &pool, const GMatchInfo &match_info,
                    GError **error_r)
{
    return resource_address_expand(&pool, &address,
                                   &match_info, error_r) &&
        transformation->ExpandChain(&pool, &match_info, error_r);
}

bool
widget_view_expand_all(struct pool *pool, struct widget_view *view,
                       const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != nullptr);
    assert(match_info != nullptr);

    while (view != nullptr) {
        if (!view->Expand(*pool, *match_info, error_r))
            return false;

        view = view->next;
    }

    return true;
}
