/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "util/StringSet.hxx"
#include "pool.hxx"

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
