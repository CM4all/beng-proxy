/*
 * Utilities for the translate.c data structures.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "transformation.hxx"
#include "processor.h"
#include "pool.hxx"

#include <string.h>

bool
Transformation::HasProcessor() const
{
    for (auto t = this; t != nullptr; t = t->next)
        if (t->type == Type::PROCESS)
            return true;

    return false;
}

bool
Transformation::IsContainer() const
{
    for (auto t = this; t != nullptr; t = t->next)
        if (t->type == Type::PROCESS)
            return (t->u.processor.options & PROCESSOR_CONTAINER) != 0;

    return false;
}

Transformation *
Transformation::Dup(struct pool *pool) const
{
    Transformation *dest = NewFromPool<Transformation>(*pool);

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
        resource_address_copy(*pool, &dest->u.filter, &u.filter);
        break;
    }

    dest->next = nullptr;
    return dest;
}

Transformation *
Transformation::DupChain(struct pool *pool) const
{
    Transformation *dest = nullptr, **tail_p = &dest;

    for (auto src = this; src != nullptr; src = src->next) {
        Transformation *p = src->Dup(pool);
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

bool
Transformation::Expand(struct pool *pool, const GMatchInfo *match_info,
                       GError **error_r)
{
    assert(pool != nullptr);
    assert(match_info != nullptr);

    switch (type) {
    case Type::PROCESS:
    case Type::PROCESS_CSS:
    case Type::PROCESS_TEXT:
        return true;

    case Type::FILTER:
        return resource_address_expand(pool, &u.filter, match_info, error_r);
    }

    assert(false);
    return true;
}

bool
Transformation::ExpandChain(struct pool *pool, const GMatchInfo *match_info,
                            GError **error_r)
{
    assert(pool != nullptr);
    assert(match_info != nullptr);

    for (auto t = this; t != nullptr; t = t->next)
        if (!t->Expand(pool, match_info, error_r))
            return false;

    return true;
}
