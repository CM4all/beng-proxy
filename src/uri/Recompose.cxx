// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Recompose.hxx"
#include "Dissect.hxx"
#include "AllocatorPtr.hxx"

char *
RecomposeUri(AllocatorPtr alloc, const DissectedUri &uri) noexcept
{
	return alloc.Concat(uri.base,
			    std::string_view{";", uri.args.data() == nullptr ? (size_t)0 : 1},
			    uri.args,
			    uri.path_info,
			    std::string_view{"?", uri.query.data() == nullptr ? (size_t)0 : 1},
			    uri.query);
}
