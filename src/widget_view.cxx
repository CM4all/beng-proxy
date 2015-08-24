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
WidgetView::Init(const char *_name)
{
    next = nullptr;
    name = _name;
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
            [HEADER_GROUP_CORS] = HEADER_FORWARD_NO,
            [HEADER_GROUP_SECURE] = HEADER_FORWARD_NO,
            [HEADER_GROUP_TRANSFORMATION] = HEADER_FORWARD_NO,
        },
    };

    response_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CORS] = HEADER_FORWARD_NO,
            [HEADER_GROUP_SECURE] = HEADER_FORWARD_NO,
            [HEADER_GROUP_TRANSFORMATION] = HEADER_FORWARD_MANGLE,
        },
    };
}

void
WidgetView::CopyFrom(struct pool &pool, const WidgetView &src)
{
    Init(p_strdup_checked(&pool, src.name));

    address.CopyFrom(pool, src.address);
    filter_4xx = src.filter_4xx;
    inherited = src.inherited;
    transformation = src.transformation->DupChain(&pool);
    request_header_forward = src.request_header_forward;
    response_header_forward = src.response_header_forward;
}

WidgetView *
WidgetView::Clone(struct pool &pool) const
{
    auto dest = NewFromPool<WidgetView>(pool);
    dest->CopyFrom(pool, *this);
    return dest;
}

void
WidgetView::CopyChainFrom(struct pool &pool, const WidgetView &_src)
{
    CopyFrom(pool, _src);

    next = nullptr;
    WidgetView **tail_p = &next;

    for (const WidgetView *src = _src.next; src != nullptr; src = src->next) {
        WidgetView *p = src->Clone(pool);
        *tail_p = p;
        tail_p = &p->next;
    }
}

WidgetView *
WidgetView::CloneChain(struct pool &pool) const
{
    assert(name == nullptr);

    WidgetView *dest = nullptr, **tail_p = &dest;

    for (const WidgetView *src = this; src != nullptr; src = src->next) {
        WidgetView *p = src->Clone(pool);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;

}

bool
WidgetView::InheritAddress(struct pool &pool,
                           const ResourceAddress &src)
{
    if (address.type != RESOURCE_ADDRESS_NONE ||
        src.type == RESOURCE_ADDRESS_NONE)
        return false;

    address.CopyFrom(pool, src);
    inherited = true;
    return true;
}

bool
WidgetView::InheritFrom(struct pool &pool, const WidgetView &src)
{
    if (InheritAddress(pool, src.address)) {
        filter_4xx = src.filter_4xx;

        request_header_forward = src.request_header_forward;
        response_header_forward = src.response_header_forward;

        return true;
    } else
        return false;
}

const WidgetView *
widget_view_lookup(const WidgetView *view, const char *name)
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
WidgetView::HasProcessor() const
{
    return transformation->HasProcessor();
}

bool
WidgetView::IsContainer() const
{
    return transformation->IsContainer();
}

bool
WidgetView::IsExpandable() const
{
    return address.IsExpandable() ||
        transformation->IsChainExpandable();
}

bool
widget_view_any_is_expandable(const WidgetView *view)
{
    while (view != nullptr) {
        if (view->IsExpandable())
            return true;

        view = view->next;
    }

    return false;
}

bool
WidgetView::Expand(struct pool &pool, const MatchInfo &match_info,
                    Error &error_r)
{
    return address.Expand(pool, match_info, error_r) &&
        transformation->ExpandChain(&pool, match_info, error_r);
}

bool
widget_view_expand_all(struct pool *pool, WidgetView *view,
                       const MatchInfo &match_info, Error &error_r)
{
    assert(pool != nullptr);

    while (view != nullptr) {
        if (!view->Expand(*pool, match_info, error_r))
            return false;

        view = view->next;
    }

    return true;
}
