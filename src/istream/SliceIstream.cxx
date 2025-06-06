// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SliceIstream.hxx"
#include "memory/SliceBuffer.hxx"

SliceIstream::SliceIstream(struct pool &p, SliceBuffer &&src) noexcept
	:MemoryIstream(p, src.Read()),
	 allocation(src.StealAllocation()) {}
