/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "View.hxx"
#include "translation/Transformation.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

void
WidgetView::CopyFrom(AllocatorPtr alloc, const WidgetView &src) noexcept
{
    name = alloc.CheckDup(src.name);
    address.CopyFrom(alloc, src.address);
    filter_4xx = src.filter_4xx;
    inherited = src.inherited;
    transformation = Transformation::DupChain(alloc, src.transformation);
    request_header_forward = src.request_header_forward;
    response_header_forward = src.response_header_forward;
}

WidgetView *
WidgetView::Clone(AllocatorPtr alloc) const noexcept
{
    auto dest = alloc.New<WidgetView>(nullptr);
    dest->CopyFrom(alloc, *this);
    return dest;
}

void
WidgetView::CopyChainFrom(AllocatorPtr alloc, const WidgetView &_src) noexcept
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
WidgetView::CloneChain(AllocatorPtr alloc) const noexcept
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
                           const ResourceAddress &src) noexcept
{
    if (address.type != ResourceAddress::Type::NONE ||
        src.type == ResourceAddress::Type::NONE)
        return false;

    address.CopyFrom(alloc, src);
    inherited = true;
    return true;
}

bool
WidgetView::InheritFrom(AllocatorPtr alloc, const WidgetView &src) noexcept
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
widget_view_lookup(const WidgetView *view, const char *name) noexcept
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
WidgetView::HasProcessor() const noexcept
{
    return Transformation::HasProcessor(transformation);
}

bool
WidgetView::IsContainer() const noexcept
{
    return Transformation::IsContainer(transformation);
}

bool
WidgetView::IsExpandable() const noexcept
{
    return address.IsExpandable() ||
        (transformation != nullptr &&
         transformation->IsChainExpandable());
}

bool
widget_view_any_is_expandable(const WidgetView *view) noexcept
{
    while (view != nullptr) {
        if (view->IsExpandable())
            return true;

        view = view->next;
    }

    return false;
}

void
WidgetView::Expand(AllocatorPtr alloc, const MatchInfo &match_info) noexcept
{
    address.Expand(alloc, match_info);
    if (transformation != nullptr)
        transformation->ExpandChain(alloc, match_info);
}

void
widget_view_expand_all(AllocatorPtr alloc, WidgetView *view,
                       const MatchInfo &match_info) noexcept
{
    while (view != nullptr) {
        view->Expand(alloc, match_info);
        view = view->next;
    }
}
