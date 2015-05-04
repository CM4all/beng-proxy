/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strset.hxx"
#include "pool.hxx"

#include <string.h>

bool
strset_contains(const struct strset *s, const char *p)
{
    strset_for_each_item(item, s)
        if (strcmp(item->value, p) == 0)
            return true;

    return false;
}

void
strset_add(struct pool *pool, struct strset *s, const char *p)
{
    auto *item = NewFromPool<struct strset_item>(*pool);
    item->value = p;
    item->next = s->head;
    s->head = item;
}

void
strset_copy(struct pool *pool, struct strset *d, const struct strset *s)
{
    strset_for_each_item(item, s)
        strset_add(pool, d, p_strdup(pool, item->value));
}
