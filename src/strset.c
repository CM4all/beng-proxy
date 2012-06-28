/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strset.h"
#include "pool.h"

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
    struct strset_item *item = p_malloc(pool, sizeof(*item));
    item->value = p;
    item->next = s->head;
    s->head = item;
}

void
strset_copy(struct pool *pool, struct strset *d, const struct strset *s)
{
    strset_for_each_item(item, s)
        strset_add(pool, d, item->value);
}
