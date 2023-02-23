// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UnusedPtr.hxx"
#include "istream.hxx"

#include <assert.h>

off_t
UnusedIstreamPtr::GetAvailable(bool partial) const noexcept
{
	assert(stream != nullptr);

	return stream->GetAvailable(partial);
}

int
UnusedIstreamPtr::AsFd() noexcept
{
	assert(stream != nullptr);

	int fd = stream->AsFd();
	if (fd >= 0)
		stream = nullptr;

	return fd;
}

void
UnusedIstreamPtr::Close(Istream &i) noexcept
{
	i.CloseUnused();
}
