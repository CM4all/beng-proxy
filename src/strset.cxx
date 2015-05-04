/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strset.hxx"
#include "pool.hxx"

#include <string.h>

bool
StringSet::Contains(const char *p) const
{
    for (auto i : *this)
        if (strcmp(i, p) == 0)
            return true;

    return false;
}

void
StringSet::Add(struct pool &pool, const char *p)
{
    auto *item = NewFromPool<Item>(pool);
    item->value = p;
    list.push_front(*item);
}

void
StringSet::CopyFrom(struct pool &pool, const StringSet &s)
{
    for (auto i : s)
        Add(pool, p_strdup(&pool, i));
}
