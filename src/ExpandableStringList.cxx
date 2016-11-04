/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ExpandableStringList.hxx"
#include "AllocatorPtr.hxx"
#include "pexpand.hxx"
#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <assert.h>

ExpandableStringList::ExpandableStringList(AllocatorPtr alloc,
                                           const ExpandableStringList &src)
{
    Builder builder(*this);

    for (const auto *i = src.head; i != nullptr; i = i->next)
        builder.Add(alloc, alloc.Dup(i->value), i->expandable);
}

bool
ExpandableStringList::IsExpandable() const
{
    for (const auto *i = head; i != nullptr; i = i->next)
        if (i->expandable)
            return true;

    return false;
}

void
ExpandableStringList::Expand(struct pool *pool, const MatchInfo &match_info)
{
    for (auto *i = head; i != nullptr; i = i->next) {
        if (!i->expandable)
            continue;

        i->value = expand_string_unescaped(pool, i->value, match_info);
    }
}

void
ExpandableStringList::Builder::Add(AllocatorPtr alloc,
                                   const char *value, bool expandable)
{
    assert(list != nullptr);

    auto *item = last = alloc.New<Item>(value, expandable);
    *tail_r = item;
    tail_r = &item->next;
}

ConstBuffer<const char *>
ExpandableStringList::ToArray(AllocatorPtr alloc) const
{
    const size_t n = std::distance(begin(), end());
    const char **p = alloc.NewArray<const char *>(n);
    std::copy(begin(), end(), p);
    return {p, n};
}
