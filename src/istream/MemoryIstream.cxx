// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "MemoryIstream.hxx"
#include "Bucket.hxx"

void
MemoryIstream::_Read() noexcept
{
	if (!data.empty()) {
		auto nbytes = InvokeData(data);
		if (nbytes == 0)
			return;

		data = data.subspan(nbytes);
	}

	if (data.empty())
		DestroyEof();
}

void
MemoryIstream::_FillBucketList(IstreamBucketList &list) noexcept
{
	if (!data.empty())
		list.Push(data);
}

Istream::ConsumeBucketResult
MemoryIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	if (nbytes > data.size())
		nbytes = data.size();
	data = data.subspan(nbytes);
	return {Consumed(nbytes), data.empty()};
}
