/*
 * Utilities for the translate.c data structures.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Transformation.hxx"
#include "processor.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

bool
Transformation::HasProcessor(const Transformation *t)
{
    for (; t != nullptr; t = t->next)
        if (t->type == Type::PROCESS)
            return true;

    return false;
}

bool
Transformation::IsContainer(const Transformation *t)
{
    for (; t != nullptr; t = t->next)
        if (t->type == Type::PROCESS)
            return (t->u.processor.options & PROCESSOR_CONTAINER) != 0;

    return false;
}

Transformation *
Transformation::Dup(AllocatorPtr alloc) const
{
    Transformation *dest = alloc.New<Transformation>();

    dest->type = type;
    switch (dest->type) {
    case Type::PROCESS:
        dest->u.processor.options = u.processor.options;
        break;

    case Type::PROCESS_CSS:
        dest->u.css_processor.options = u.css_processor.options;
        break;

    case Type::PROCESS_TEXT:
        break;

    case Type::FILTER:
        dest->u.filter.address.CopyFrom(alloc, u.filter.address);
        dest->u.filter.reveal_user = u.filter.reveal_user;
        break;
    }

    dest->next = nullptr;
    return dest;
}

Transformation *
Transformation::DupChain(AllocatorPtr alloc, const Transformation *src)
{
    Transformation *dest = nullptr, **tail_p = &dest;

    for (; src != nullptr; src = src->next) {
        Transformation *p = src->Dup(alloc);
        *tail_p = p;
        tail_p = &p->next;
    }

    return dest;
}

bool
Transformation::IsChainExpandable() const
{
    for (auto t = this; t != nullptr; t = t->next)
        if (t->IsExpandable())
            return true;

    return false;
}

void
Transformation::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    switch (type) {
    case Type::PROCESS:
    case Type::PROCESS_CSS:
    case Type::PROCESS_TEXT:
        break;

    case Type::FILTER:
        u.filter.address.Expand(alloc, match_info);
        break;
    }
}

void
Transformation::ExpandChain(AllocatorPtr alloc, const MatchInfo &match_info)
{
    for (auto t = this; t != nullptr; t = t->next)
        t->Expand(alloc, match_info);
}
