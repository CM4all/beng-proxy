/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "param_array.h"
#include "pool.h"

void
param_array_copy(struct pool *pool, struct param_array *dest,
                 const struct param_array *src)
{
    dest->n = src->n;
    for (unsigned i = 0; i < src->n; ++i)
        dest->values[i] = p_strdup(pool, src->values[i]);
}
