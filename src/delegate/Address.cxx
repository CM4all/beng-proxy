/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Address.hxx"
#include "pool.hxx"

DelegateAddress::DelegateAddress(const char *_delegate)
    :delegate(_delegate)
{
    child_options.Init();
}

DelegateAddress::DelegateAddress(struct pool &pool, const DelegateAddress &src)
    :delegate(p_strdup(&pool, src.delegate)) {
    child_options.CopyFrom(&pool, &src.child_options);
}

bool
DelegateAddress::Expand(struct pool &pool, const MatchInfo &match_info,
                        Error &error_r)
{
    if (!child_options.Expand(pool, match_info, error_r))
        return false;

    return true;
}
