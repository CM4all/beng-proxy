// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "MultiFifoBuffer.hxx"
#include "istream/Bucket.hxx"

void
MultiFifoBuffer::FillBucketList(IstreamBucketList &list) const noexcept
{
	for (const std::span<const std::byte> i : *this)
		list.Push(i);
}
