// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
DelegateAddress::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	child_options.Expand(alloc, match_data);
}
