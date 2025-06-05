// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "istream_memory.hxx"
#include "MemoryIstream.hxx"
#include "New.hxx"

UnusedIstreamPtr
istream_memory_new(struct pool &pool, std::span<const std::byte> src) noexcept
{
	return NewIstreamPtr<MemoryIstream>(pool, src);
}
