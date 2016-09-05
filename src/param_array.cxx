/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "param_array.hxx"
#include "pool.hxx"
#include "pexpand.hxx"

param_array::param_array(struct pool &pool, const struct param_array &src)
{
    CopyFrom(&pool, src);
}

void
param_array::CopyFrom(struct pool *pool, const struct param_array &src)
{
    n = src.n;
    for (unsigned i = 0; i < src.n; ++i) {
        values[i] = p_strdup(pool, src.values[i]);
        expand_values[i] = src.expand_values[i];
    }
}

bool
param_array::IsExpandable() const
{
    for (unsigned i = 0; i < n; ++i)
        if (expand_values[i])
            return true;

    return false;
}

bool
param_array::Expand(struct pool *pool,
                    const MatchInfo &match_info, Error &error_r)
{
    assert(pool != nullptr);

    for (unsigned i = 0; i < n; ++i) {
        if (!expand_values[i])
            continue;

        values[i] = expand_string_unescaped(pool, values[i],
                                            match_info, error_r);
        if (values[i] == nullptr)
            return false;
    }

    return true;
}
