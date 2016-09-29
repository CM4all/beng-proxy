/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ExpandableStringList.hxx"
#include "pool.hxx"
#include "pexpand.hxx"
#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <assert.h>

ExpandableStringList::ExpandableStringList(struct pool &pool,
                                           const ExpandableStringList &src)
{
    Builder builder(*this);

    for (const auto *i = src.head; i != nullptr; i = i->next)
        builder.Add(pool, p_strdup(&pool, i->value), i->expandable);
}

bool
ExpandableStringList::IsExpandable() const
{
    for (const auto *i = head; i != nullptr; i = i->next)
        if (i->expandable)
            return true;

    return false;
}

bool
ExpandableStringList::Expand(struct pool *pool,
                             const MatchInfo &match_info, Error &error_r)
{
    for (auto *i = head; i != nullptr; i = i->next) {
        if (!i->expandable)
            continue;

        i->value = expand_string_unescaped(pool, i->value,
                                           match_info, error_r);
        if (i->value == nullptr)
            return false;
    }

    return true;
}

void
ExpandableStringList::Builder::Add(struct pool &pool,
                                   const char *value, bool expandable)
{
    assert(list != nullptr);

    auto *item = last = NewFromPool<Item>(pool, value, expandable);
    *tail_r = item;
    tail_r = &item->next;
}

ConstBuffer<const char *>
ExpandableStringList::ToArray(struct pool &pool) const
{
    const size_t n = std::distance(begin(), end());
    const char **p = PoolAlloc<const char *>(pool, n);
    std::copy(begin(), end(), p);
    return {p, n};
}
