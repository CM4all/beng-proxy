/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strset.hxx"
#include "pool.hxx"

#include <string.h>

bool
strset::Contains(const char *p) const
{
    strset_for_each_item(item, this)
        if (strcmp(item->value, p) == 0)
            return true;

    return false;
}

void
strset::Add(struct pool &pool, const char *p)
{
    auto *item = NewFromPool<struct strset_item>(pool);
    item->value = p;
    item->next = head;
    head = item;
}

void
strset::CopyFrom(struct pool &pool, const struct strset &s)
{
    strset_for_each_item(item, &s)
        Add(pool, p_strdup(&pool, item->value));
}
