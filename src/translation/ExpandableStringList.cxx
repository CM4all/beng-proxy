/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ExpandableStringList.hxx"
#include "AllocatorPtr.hxx"
#include "util/ConstBuffer.hxx"

#if TRANSLATION_ENABLE_EXPAND
#include "pexpand.hxx"
#endif

#include <algorithm>

#include <assert.h>

ExpandableStringList::ExpandableStringList(AllocatorPtr alloc,
                                           const ExpandableStringList &src)
{
    Builder builder(*this);

    for (const auto *i = src.head; i != nullptr; i = i->next)
        builder.Add(alloc, alloc.Dup(i->value),
#if TRANSLATION_ENABLE_EXPAND
                    i->expandable
#else
                    false
#endif
                    );
}

#if TRANSLATION_ENABLE_EXPAND

bool
ExpandableStringList::IsExpandable() const
{
    for (const auto *i = head; i != nullptr; i = i->next)
        if (i->expandable)
            return true;

    return false;
}

void
ExpandableStringList::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    for (auto *i = head; i != nullptr; i = i->next) {
        if (!i->expandable)
            continue;

        i->value = expand_string_unescaped(alloc, i->value, match_info);
    }
}

#endif

void
ExpandableStringList::Builder::Add(AllocatorPtr alloc,
                                   const char *value, bool expandable)
{
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
