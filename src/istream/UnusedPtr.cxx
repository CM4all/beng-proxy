// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "UnusedPtr.hxx"
#include "istream.hxx"

#include <assert.h>

off_t
UnusedIstreamPtr::GetAvailable(bool partial) const noexcept
{
	assert(stream != nullptr);

	return stream->GetAvailable(partial);
}

void
UnusedIstreamPtr::Close(Istream &i) noexcept
{
	i.CloseUnused();
}
