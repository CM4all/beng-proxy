/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "param_array.hxx"
#include "pool.hxx"
#include "regex.hxx"

void
param_array::CopyFrom(struct pool *pool, const struct param_array &src)
{
    n = src.n;
    for (unsigned i = 0; i < src.n; ++i) {
        values[i] = p_strdup(pool, src.values[i]);
        expand_values[i] = p_strdup_checked(pool, src.expand_values[i]);
    }
}

bool
param_array::IsExpandable() const
{
    for (unsigned i = 0; i < n; ++i)
        if (expand_values[i] != nullptr)
            return true;

    return false;
}

bool
param_array::Expand(struct pool *pool,
                    const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != nullptr);
    assert(match_info != nullptr);

    for (unsigned i = 0; i < n; ++i) {
        if (expand_values[i] == nullptr)
            continue;

        values[i] = expand_string_unescaped(pool, expand_values[i],
                                            match_info, error_r);
        if (values[i] == nullptr)
            return false;
    }

    return true;
}
