/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Address.hxx"
#include "AllocatorPtr.hxx"

DelegateAddress::DelegateAddress(const char *_delegate)
    :delegate(_delegate)
{
}

DelegateAddress::DelegateAddress(AllocatorPtr alloc, const DelegateAddress &src)
    :delegate(alloc.Dup(src.delegate)),
     child_options(alloc, src.child_options) {}

void
DelegateAddress::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    child_options.Expand(alloc, match_info);
}
