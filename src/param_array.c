/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "param_array.h"
#include "pool.h"
#include "regex.h"

void
param_array_copy(struct pool *pool, struct param_array *dest,
                 const struct param_array *src)
{
    dest->n = src->n;
    for (unsigned i = 0; i < src->n; ++i) {
        dest->values[i] = p_strdup(pool, src->values[i]);
        dest->expand_values[i] = p_strdup_checked(pool, src->expand_values[i]);
    }
}

bool
param_array_is_expandable(const struct param_array *pa)
{
    for (unsigned i = 0; i < pa->n; ++i)
        if (pa->expand_values[i] != NULL)
            return true;

    return false;
}

bool
param_array_expand(struct pool *pool, struct param_array *pa,
                   const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(pa != NULL);
    assert(match_info != NULL);

    for (unsigned i = 0; i < pa->n; ++i) {
        if (pa->expand_values[i] == NULL)
            continue;

        pa->values[i] = expand_string_unescaped(pool, pa->expand_values[i],
                                                match_info, error_r);
        if (pa->values[i] == NULL)
            return false;
    }

    return true;
}
