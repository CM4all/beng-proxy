/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strset.hxx"
#include "pool.hxx"

#include <string.h>

bool
StringSet::Contains(const char *p) const
{
    strset_for_each_item(item, this)
        if (strcmp(item->value, p) == 0)
            return true;

    return false;
}

void
StringSet::Add(struct pool &pool, const char *p)
{
    auto *item = NewFromPool<Item>(pool);
    item->value = p;
    item->next = head;
    head = item;
}

void
StringSet::CopyFrom(struct pool &pool, const StringSet &s)
{
    strset_for_each_item(item, &s)
        Add(pool, p_strdup(&pool, item->value));
}
