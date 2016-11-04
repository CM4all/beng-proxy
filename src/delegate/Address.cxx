/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Address.hxx"
#include "pool.hxx"
#include "AllocatorPtr.hxx"

DelegateAddress::DelegateAddress(const char *_delegate)
    :delegate(_delegate)
{
}

DelegateAddress::DelegateAddress(struct pool &pool, const DelegateAddress &src)
    :delegate(p_strdup(&pool, src.delegate)),
     child_options(pool, src.child_options) {}

void
DelegateAddress::Expand(struct pool &pool, const MatchInfo &match_info)
{
    child_options.Expand(pool, match_info);
}
