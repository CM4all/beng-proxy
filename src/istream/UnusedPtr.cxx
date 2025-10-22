// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "UnusedPtr.hxx"
#include "istream.hxx"

#include <assert.h>

IstreamLength
UnusedIstreamPtr::GetLength() const noexcept
{
	assert(stream != nullptr);

	return stream->GetLength();
}

void
UnusedIstreamPtr::Close(Istream &i) noexcept
{
	i.CloseUnused();
}
