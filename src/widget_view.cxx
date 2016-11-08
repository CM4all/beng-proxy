/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_view.hxx"
#include "translation/Transformation.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

void
WidgetView::Init(const char *_name)
{
    next = nullptr;
    name = _name;
    address.type = ResourceAddress::Type::NONE;
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
WidgetView::CopyFrom(AllocatorPtr alloc, const WidgetView &src)
{
    Init(alloc.CheckDup(src.name));

    address.CopyFrom(alloc, src.address);
    filter_4xx = src.filter_4xx;
    inherited = src.inherited;
    transformation = Transformation::DupChain(alloc, src.transformation);
    request_header_forward = src.request_header_forward;
    response_header_forward = src.response_header_forward;
}

WidgetView *
WidgetView::Clone(AllocatorPtr alloc) const
{
    auto dest = alloc.New<WidgetView>();
    dest->CopyFrom(alloc, *this);
    return dest;
}

void
WidgetView::CopyChainFrom(AllocatorPtr alloc, const WidgetView &_src)
{
    CopyFrom(alloc, _src);

    next = nullptr;
    WidgetView **tail_p = &next;

    for (const WidgetView *src = _src.next; src != nullptr; src = src->next) {
        WidgetView *p = src->Clone(alloc);
        *tail_p = p;
        tail_p = &p->next;
    }
}

WidgetView *
WidgetView::CloneChain(AllocatorPtr alloc) const
{
    assert(name == nullptr);

    WidgetView *dest = nullptr, **tail_p = &dest;

    for (const WidgetView *src = this; src != nullptr; src = src->next) {
        WidgetView *p = src->Clone(alloc);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;

}

bool
WidgetView::InheritAddress(AllocatorPtr alloc,
                           const ResourceAddress &src)
{
    if (address.type != ResourceAddress::Type::NONE ||
        src.type == ResourceAddress::Type::NONE)
        return false;

    address.CopyFrom(alloc, src);
    inherited = true;
    return true;
}

bool
WidgetView::InheritFrom(AllocatorPtr alloc, const WidgetView &src)
{
    if (InheritAddress(alloc, src.address)) {
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

void
WidgetView::Expand(struct pool &pool, const MatchInfo &match_info)
{
    address.Expand(pool, match_info);
    transformation->ExpandChain(&pool, match_info);
}

void
widget_view_expand_all(struct pool *pool, WidgetView *view,
                       const MatchInfo &match_info)
{
    assert(pool != nullptr);

    while (view != nullptr) {
        view->Expand(*pool, match_info);
        view = view->next;
    }
}
